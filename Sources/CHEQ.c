#include "CHEQ.h"

#include <float.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * Filter equations: Robert Bristow Johnson, Audio EQ Cookbook, published by
 * the W3C Audio Working Group. The low shelf, peaking EQ, and high shelf
 * equations are implemented with a0 normalized to one.
 * https://www.w3.org/TR/audio-eq-cookbook/
 *
 * Do not compile this file with fast math: nonfinite sample checks matter.
 */

/* Keep matched numerator/denominator products identically rounded at unity.
 * Implicit FMA contraction on arm64 otherwise leaves a cancellation residual
 * after extreme finite inputs, even when a section is exactly flat. */
#pragma STDC FP_CONTRACT OFF

_Static_assert(sizeof(float) == sizeof(uint32_t), "Float32 audio is required");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24, "IEEE Float32 is required");

enum {
    CH_BANDS = 3,
    CH_CONTROL_INTERVAL = 32,
    CH_RESPONSE_POINTS = 513,
    CH_GAIN_BITS = 11,
    CH_TRIM_SHIFT = 33,
    CH_BYPASS_SHIFT = 44,
    CH_HEADROOM_SHIFT = 45
};

static const double CH_PI = 3.14159265358979323846264338327950288;
static const uint64_t CH_GAIN_MASK = UINT64_C(2047);
static const uint64_t CH_HEADROOM_MASK = UINT64_C(4095);

typedef struct {
    double b0, b1, b2, a1, a2;
} CHCoefficients;

typedef struct {
    double sine;
    double cosine;
    double frequency;
} CHFrequency;

typedef struct {
    CHCoefficients current;
    CHCoefficients end;
    CHCoefficients step;
    double x1[2], x2[2], y1[2], y2[2];
} CHFilter;

struct CHEQ {
    /* Immutable after create, also read by the control thread. */
    double sample_rate;
    CHFrequency frequency[CH_BANDS];
    double tone_alpha;
    double headroom_attack_alpha;
    double headroom_release_alpha;
    double trim_alpha;
    double bypass_alpha;

    /* A complete quantized control transaction fits in one atomic word. */
    _Atomic(uint64_t) controls;

    /* Everything below here, except the explicit atomics, belongs to audio. */
    uint64_t last_controls;
    double target_tone[CH_BANDS];
    double current_tone[CH_BANDS];
    double target_auto_gain;
    double current_auto_gain;
    double target_trim_gain;
    double current_trim_gain;
    double target_wet;
    double current_wet;
    CHFilter filters[CH_BANDS];
    unsigned control_remaining;

    double input_envelope;
    double output_envelope;
    uint64_t clipped_samples;
    uint64_t invalid_samples;

    _Atomic(uint32_t) meter_input;
    _Atomic(uint32_t) meter_output;
    _Atomic(uint32_t) meter_headroom;
    _Atomic(uint64_t) meter_clips;
    _Atomic(uint64_t) meter_invalid;
};

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits_float(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void count_event(uint64_t *counter) {
    if (*counter != UINT64_MAX) {
        ++*counter;
    }
}

static int quantize_db(float value, double low, double high) {
    double finite_value = isfinite(value) ? (double)value : 0.0;
    if (finite_value < low) finite_value = low;
    if (finite_value > high) finite_value = high;
    return (int)lround(finite_value * 100.0);
}

static uint64_t pack_controls(const int tone[CH_BANDS], int trim,
                              bool bypass, unsigned headroom) {
    return (uint64_t)(tone[0] + 600)
         | ((uint64_t)(tone[1] + 600) << 11)
         | ((uint64_t)(tone[2] + 600) << 22)
         | ((uint64_t)(-trim) << CH_TRIM_SHIFT)
         | ((uint64_t)bypass << CH_BYPASS_SHIFT)
         | ((uint64_t)headroom << CH_HEADROOM_SHIFT);
}

static CHCoefficients make_coefficients(const CHFrequency *frequency,
                                        unsigned band, double gain_db) {
    const double A = pow(10.0, gain_db / 40.0);
    const double c = frequency->cosine;
    const double s = frequency->sine;
    double b0, b1, b2, a0, a1, a2;

    if (band == 1) {
        const double alpha = s / (2.0 * 0.8);
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * c;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * c;
        a2 = 1.0 - alpha / A;
    } else {
        /* S = 1 gives alpha = sin(w0) / sqrt(2). */
        const double beta = sqrt(2.0 * A) * s;
        const double ap = A + 1.0;
        const double am = A - 1.0;
        if (band == 0) {
            b0 = A * (ap - am * c + beta);
            b1 = 2.0 * A * (am - ap * c);
            b2 = A * (ap - am * c - beta);
            a0 = ap + am * c + beta;
            a1 = -2.0 * (am + ap * c);
            a2 = ap + am * c - beta;
        } else {
            b0 = A * (ap + am * c + beta);
            b1 = -2.0 * A * (am + ap * c);
            b2 = A * (ap + am * c - beta);
            a0 = ap - am * c + beta;
            a1 = 2.0 * (am - ap * c);
            a2 = ap - am * c - beta;
        }
    }
    const CHCoefficients result = {
        b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0
    };
    return result;
}

static double response_power(const CHCoefficients coefficients[CH_BANDS],
                             double radians) {
    const double c1 = cos(radians);
    const double s1 = sin(radians);
    const double c2 = 2.0 * c1 * c1 - 1.0;
    const double s2 = 2.0 * s1 * c1;
    double result = 1.0;
    for (unsigned band = 0; band < CH_BANDS; ++band) {
        const CHCoefficients *c = &coefficients[band];
        const double nr = c->b0 + c->b1 * c1 + c->b2 * c2;
        const double ni = c->b1 * s1 + c->b2 * s2;
        const double dr = 1.0 + c->a1 * c1 + c->a2 * c2;
        const double di = c->a1 * s1 + c->a2 * s2;
        const double denominator = dr * dr + di * di;
        if (denominator <= DBL_MIN) return INFINITY;
        result *= (nr * nr + ni * ni) / denominator;
    }
    return result;
}

/* Runs on the setter thread, never in the audio callback. */
static unsigned estimate_headroom(const CHEQ *eq, const int tone[CH_BANDS]) {
    if (tone[0] == 0 && tone[1] == 0 && tone[2] == 0) return 0;

    CHCoefficients coefficients[CH_BANDS];
    double fallback_db = 1.0;
    for (unsigned band = 0; band < CH_BANDS; ++band) {
        const double db = (double)tone[band] / 100.0;
        coefficients[band] = make_coefficients(&eq->frequency[band], band, db);
        fallback_db += fmax(0.0, db);
    }

    /* Include DC, Nyquist, exact centers, and a dense logarithmic grid. */
    double maximum = fmax(1.0, response_power(coefficients, 0.0));
    maximum = fmax(maximum, response_power(coefficients, CH_PI));
    for (unsigned band = 0; band < CH_BANDS; ++band) {
        const double w = 2.0 * CH_PI * eq->frequency[band].frequency
                       / eq->sample_rate;
        maximum = fmax(maximum, response_power(coefficients, w));
    }
    const double start_hz = 5.0;
    const double ratio = pow((eq->sample_rate * 0.5) / start_hz,
                             1.0 / (CH_RESPONSE_POINTS - 1));
    double hz = start_hz;
    for (unsigned i = 0; i < CH_RESPONSE_POINTS; ++i, hz *= ratio) {
        const double w = 2.0 * CH_PI * hz / eq->sample_rate;
        maximum = fmax(maximum, response_power(coefficients, w));
    }
    const double sampled_db = 10.0 * log10(maximum) + 1.0;
    const double db = isfinite(sampled_db) ? sampled_db : fallback_db;
    const double cents = ceil(fmax(0.0, db) * 100.0);
    return (unsigned)fmin(cents, (double)CH_HEADROOM_MASK);
}

CHEQ *cheq_create(double sampleRate) {
    if (!isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
        return NULL;
    }
    CHEQ *eq = calloc(1, sizeof(*eq));
    if (!eq) return NULL;

    const int flat[CH_BANDS] = {0, 0, 0};
    const uint64_t initial = pack_controls(flat, 0, false, 0);
    atomic_init(&eq->controls, initial);
    atomic_init(&eq->meter_input, float_bits(0.0f));
    atomic_init(&eq->meter_output, float_bits(0.0f));
    atomic_init(&eq->meter_headroom, float_bits(0.0f));
    atomic_init(&eq->meter_clips, 0);
    atomic_init(&eq->meter_invalid, 0);
    if (!atomic_is_lock_free(&eq->controls)
        || !atomic_is_lock_free(&eq->meter_input)
        || !atomic_is_lock_free(&eq->meter_clips)) {
        free(eq);
        return NULL;
    }

    eq->sample_rate = sampleRate;
    eq->last_controls = initial;
    eq->tone_alpha = 1.0 - exp(-CH_CONTROL_INTERVAL / (0.050 * sampleRate));
    eq->headroom_attack_alpha = 1.0 - exp(-1.0 / (0.004 * sampleRate));
    eq->headroom_release_alpha = 1.0 - exp(-1.0 / (0.250 * sampleRate));
    eq->trim_alpha = 1.0 - exp(-1.0 / (0.020 * sampleRate));
    eq->bypass_alpha = 1.0 - exp(-1.0 / (0.010 * sampleRate));
    eq->target_auto_gain = eq->current_auto_gain = 1.0;
    eq->target_trim_gain = eq->current_trim_gain = 1.0;
    eq->target_wet = eq->current_wet = 1.0;
    const double frequencies[CH_BANDS] = {150.0, 900.0, 4500.0};
    for (unsigned band = 0; band < CH_BANDS; ++band) {
        CHFrequency *f = &eq->frequency[band];
        f->frequency = fmin(frequencies[band], 0.4 * sampleRate);
        const double w = 2.0 * CH_PI * f->frequency / sampleRate;
        f->sine = sin(w);
        f->cosine = cos(w);
        eq->filters[band].current = make_coefficients(f, band, 0.0);
        eq->filters[band].end = eq->filters[band].current;
    }
    return eq;
}

void cheq_destroy(CHEQ *eq) {
    free(eq);
}

void cheq_set_controls(CHEQ *eq, float bass, float mid, float treble,
                      float trim, bool bypass) {
    if (!eq) return;
    const int tone[CH_BANDS] = {
        quantize_db(bass, -6.0, 6.0),
        quantize_db(mid, -6.0, 6.0),
        quantize_db(treble, -6.0, 6.0)
    };
    const int trim_cents = quantize_db(trim, -12.0, 0.0);
    const unsigned headroom = estimate_headroom(eq, tone);
    const uint64_t packet = pack_controls(tone, trim_cents, bypass, headroom);
    atomic_store_explicit(&eq->controls, packet, memory_order_release);
}

static CHCoefficients coefficient_step(CHCoefficients from, CHCoefficients to) {
    const double amount = 1.0 / CH_CONTROL_INTERVAL;
    const CHCoefficients step = {
        (to.b0 - from.b0) * amount,
        (to.b1 - from.b1) * amount,
        (to.b2 - from.b2) * amount,
        (to.a1 - from.a1) * amount,
        (to.a2 - from.a2) * amount
    };
    return step;
}

static void start_control_interval(CHEQ *eq) {
    const uint64_t packet = atomic_load_explicit(&eq->controls, memory_order_acquire);
    if (packet != eq->last_controls) {
        for (unsigned band = 0; band < CH_BANDS; ++band) {
            const int cents = (int)((packet >> (band * CH_GAIN_BITS))
                                    & CH_GAIN_MASK) - 600;
            eq->target_tone[band] = cents / 100.0;
        }
        const double trim_db = -(double)((packet >> CH_TRIM_SHIFT) & CH_GAIN_MASK)
                             / 100.0;
        const double headroom_db = (double)((packet >> CH_HEADROOM_SHIFT)
                                            & CH_HEADROOM_MASK) / 100.0;
        eq->target_auto_gain = pow(10.0, -headroom_db / 20.0);
        eq->target_trim_gain = pow(10.0, trim_db / 20.0);
        eq->target_wet = ((packet >> CH_BYPASS_SHIFT) & 1) ? 0.0 : 1.0;
        eq->last_controls = packet;
    }

    for (unsigned band = 0; band < CH_BANDS; ++band) {
        double gain = eq->current_tone[band];
        gain += eq->tone_alpha * (eq->target_tone[band] - gain);
        if (fabs(eq->target_tone[band] - gain) < 1e-7) {
            gain = eq->target_tone[band];
        }
        eq->current_tone[band] = gain;
        CHFilter *filter = &eq->filters[band];
        filter->end = make_coefficients(&eq->frequency[band], band, gain);
        filter->step = coefficient_step(filter->current, filter->end);
    }
    eq->control_remaining = CH_CONTROL_INTERVAL;
}

static double smooth(double current, double target, double alpha) {
    current += alpha * (target - current);
    return fabs(target - current) < 1e-10 ? target : current;
}

static double finite_input(float value, CHEQ *eq) {
    if (isfinite(value)) return value;
    count_event(&eq->invalid_samples);
    return 0.0;
}

static void clear_channel_after_fault(CHEQ *eq, unsigned channel) {
    for (unsigned band = 0; band < CH_BANDS; ++band) {
        CHFilter *f = &eq->filters[band];
        f->x1[channel] = f->x2[channel] = 0.0;
        f->y1[channel] = f->y2[channel] = 0.0;
    }
}

static double filter_channel(CHEQ *eq, unsigned channel, double input) {
    double x = input;
    for (unsigned band = 0; band < CH_BANDS; ++band) {
        CHFilter *f = &eq->filters[band];
        const CHCoefficients *c = &f->current;
        /* Group matched numerator and denominator terms for flat identity. */
        double y = c->b0 * x
                 + (c->b1 * f->x1[channel] - c->a1 * f->y1[channel])
                 + (c->b2 * f->x2[channel] - c->a2 * f->y2[channel]);
        if (!isfinite(y)) {
            count_event(&eq->invalid_samples);
            clear_channel_after_fault(eq, channel);
            return 0.0;
        }
        /* Tiny inaudible filter tails are flushed before becoming denormals. */
        if (fabs(y) < 1e-30) y = 0.0;
        f->x2[channel] = f->x1[channel];
        f->x1[channel] = x;
        f->y2[channel] = f->y1[channel];
        f->y1[channel] = y;
        x = y;
    }
    return x;
}

static float safe_output(CHEQ *eq, double value) {
    if (!isfinite(value)) {
        count_event(&eq->invalid_samples);
        return 0.0f;
    }
    if (value > 1.0) {
        count_event(&eq->clipped_samples);
        return 1.0f;
    }
    if (value < -1.0) {
        count_event(&eq->clipped_samples);
        return -1.0f;
    }
    return (float)value;
}

void cheq_process_stereo(CHEQ *eq,
                        const float *inL, size_t inLStride,
                        const float *inR, size_t inRStride,
                        float *outL, size_t outLStride,
                        float *outR, size_t outRStride,
                        size_t frames) {
    if (!eq || !inL || !inR || !outL || !outR || !frames
        || !inLStride || !inRStride || !outLStride || !outRStride) {
        return;
    }

    double input_peak = 0.0;
    double output_peak = 0.0;
    for (size_t frame = 0; frame < frames; ++frame) {
        if (!eq->control_remaining) start_control_interval(eq);

        for (unsigned band = 0; band < CH_BANDS; ++band) {
            CHFilter *f = &eq->filters[band];
            f->current.b0 += f->step.b0;
            f->current.b1 += f->step.b1;
            f->current.b2 += f->step.b2;
            f->current.a1 += f->step.a1;
            f->current.a2 += f->step.a2;
        }
        const double attack = eq->target_auto_gain < eq->current_auto_gain
                            ? eq->headroom_attack_alpha
                            : eq->headroom_release_alpha;
        eq->current_auto_gain = smooth(eq->current_auto_gain,
                                       eq->target_auto_gain, attack);
        eq->current_trim_gain = smooth(eq->current_trim_gain,
                                       eq->target_trim_gain, eq->trim_alpha);
        eq->current_wet = smooth(eq->current_wet, eq->target_wet, eq->bypass_alpha);
        const double gain = eq->current_auto_gain * eq->current_trim_gain;

        /* Both reads precede writes, including for interleaved in place audio. */
        const double left = finite_input(*inL, eq);
        const double right = finite_input(*inR, eq);
        input_peak = fmax(input_peak, fmax(fabs(left), fabs(right)));
        const double wet_l = filter_channel(eq, 0, left);
        const double wet_r = filter_channel(eq, 1, right);
        const double mix_l = left + eq->current_wet * (wet_l - left);
        const double mix_r = right + eq->current_wet * (wet_r - right);
        const float result_l = safe_output(eq, mix_l * gain);
        const float result_r = safe_output(eq, mix_r * gain);
        *outL = result_l;
        *outR = result_r;
        output_peak = fmax(output_peak, fmax(fabs(result_l), fabs(result_r)));

        --eq->control_remaining;
        if (!eq->control_remaining) {
            for (unsigned band = 0; band < CH_BANDS; ++band) {
                eq->filters[band].current = eq->filters[band].end;
            }
        }
        if (frame + 1 < frames) {
            inL += inLStride;
            inR += inRStride;
            outL += outLStride;
            outR += outRStride;
        }
    }

    const double decay = exp(-(double)frames / (0.300 * eq->sample_rate));
    eq->input_envelope = fmax(input_peak, eq->input_envelope * decay);
    eq->output_envelope = fmax(output_peak, eq->output_envelope * decay);
    const float headroom_db = (float)(-20.0 * log10(eq->current_auto_gain));
    atomic_store_explicit(&eq->meter_input, float_bits((float)eq->input_envelope),
                           memory_order_relaxed);
    atomic_store_explicit(&eq->meter_output, float_bits((float)eq->output_envelope),
                           memory_order_relaxed);
    atomic_store_explicit(&eq->meter_headroom, float_bits(headroom_db),
                           memory_order_relaxed);
    atomic_store_explicit(&eq->meter_clips, eq->clipped_samples, memory_order_relaxed);
    atomic_store_explicit(&eq->meter_invalid, eq->invalid_samples, memory_order_relaxed);
}

void cheq_get_meters(const CHEQ *eq, CHEQMeters *meters) {
    if (!meters) return;
    if (!eq) {
        memset(meters, 0, sizeof(*meters));
        return;
    }
    meters->input_peak = bits_float(atomic_load_explicit(&eq->meter_input,
                                                        memory_order_relaxed));
    meters->output_peak = bits_float(atomic_load_explicit(&eq->meter_output,
                                                         memory_order_relaxed));
    meters->headroom_db = bits_float(atomic_load_explicit(&eq->meter_headroom,
                                                         memory_order_relaxed));
    meters->clipped_samples = atomic_load_explicit(&eq->meter_clips, memory_order_relaxed);
    meters->invalid_samples = atomic_load_explicit(&eq->meter_invalid, memory_order_relaxed);
}

float cheq_peak(const CHEQ *eq) {
    if (!eq) return 0.0f;
    return bits_float(atomic_load_explicit(&eq->meter_output, memory_order_relaxed));
}

uint64_t cheq_clip_count(const CHEQ *eq) {
    if (!eq) return 0;
    return atomic_load_explicit(&eq->meter_clips, memory_order_relaxed);
}
