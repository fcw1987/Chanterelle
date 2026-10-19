#include "CHEQ.h"

#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double PI = 3.14159265358979323846264338327950288;
static unsigned completed_tests = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void passed(const char *name) {
    ++completed_tests;
    printf("PASS %s\n", name);
}

static uint32_t rng_state = UINT32_C(0x31415926);

static float random_sample(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(((double)rng_state / UINT32_MAX) * 2.0 - 1.0);
}

static void process_planar(CHEQ *eq, const float *l, const float *r,
                           float *ol, float *or_, size_t n) {
    cheq_process_stereo(eq, l, 1, r, 1, ol, 1, or_, 1, n);
}

static void silence(CHEQ *eq, size_t frames) {
    float zero[256] = {0};
    float out_l[256], out_r[256];
    while (frames) {
        size_t count = frames > 256 ? 256 : frames;
        process_planar(eq, zero, zero, out_l, out_r, count);
        frames -= count;
    }
}

static void test_create_and_nulls(void) {
    const double bad_rates[] = {0, -1, 7999, 384001, NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof(bad_rates) / sizeof(bad_rates[0]); ++i) {
        CHECK(cheq_create(bad_rates[i]) == NULL);
    }
    CHEQ *eq = cheq_create(48000);
    CHECK(eq);
    float x = 0.5f, y = 0.25f;
    cheq_process_stereo(eq, &x, 1, &x, 1, &y, 1, &y, 1, 0);
    CHECK(y == 0.25f);
    cheq_process_stereo(eq, &x, 0, &x, 1, &y, 1, &y, 1, 1);
    CHECK(y == 0.25f);
    cheq_process_stereo(eq, NULL, 1, &x, 1, &y, 1, &y, 1, 1);
    CHECK(y == 0.25f);
    cheq_process_stereo(NULL, &x, 1, &x, 1, &y, 1, &y, 1, 1);
    cheq_set_controls(NULL, 0, 0, 0, 0, false);
    cheq_get_meters(eq, NULL);
    CHEQMeters meter;
    memset(&meter, 0xff, sizeof(meter));
    cheq_get_meters(NULL, &meter);
    CHECK(meter.input_peak == 0 && meter.output_peak == 0);
    CHECK(meter.headroom_db == 0 && meter.clipped_samples == 0);
    CHECK(meter.invalid_samples == 0);
    CHECK(cheq_peak(NULL) == 0 && cheq_clip_count(NULL) == 0);
    cheq_destroy(eq);
    cheq_destroy(NULL);
    passed("creation validation and null handling");
}

static void test_flat_identity(void) {
    const double rates[] = {8000, 44100, 48000, 96000, 192000, 384000};
    float l[1009], r[1009], ol[1009], or_[1009];
    for (size_t i = 0; i < 1009; ++i) {
        l[i] = random_sample() * 0.95f;
        r[i] = random_sample() * 0.95f;
    }
    l[0] = 1.0f;
    r[0] = -1.0f;
    l[1] = r[1] = 0.0f;
    for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        CHEQ *eq = cheq_create(rates[rate]);
        CHECK(eq);
        process_planar(eq, l, r, ol, or_, 1009);
        CHECK(memcmp(l, ol, sizeof(l)) == 0);
        CHECK(memcmp(r, or_, sizeof(r)) == 0);
        CHECK(cheq_peak(eq) == 1.0f);
        CHECK(cheq_clip_count(eq) == 0);
        cheq_destroy(eq);
    }
    passed("exact flat identity at six sample rates");
}

/* Returns measured tone gain after removing the reported automatic trim. */
static double measure_response(float bass, float mid, float treble,
                               double frequency, double sample_rate) {
    CHEQ *eq = cheq_create(sample_rate);
    CHECK(eq);
    cheq_set_controls(eq, bass, mid, treble, 0, false);
    silence(eq, (size_t)sample_rate);
    const size_t total = (size_t)(sample_rate * 1.0);
    double input_energy = 0, output_energy = 0;
    float l[256], r[256] = {0}, ol[256], or_[256];
    for (size_t offset = 0; offset < total; offset += 256) {
        size_t n = total - offset > 256 ? 256 : total - offset;
        for (size_t i = 0; i < n; ++i) {
            l[i] = (float)(0.15 * sin(2 * PI * frequency * (offset + i) / sample_rate));
        }
        process_planar(eq, l, r, ol, or_, n);
        if (offset >= total / 2) {
            for (size_t i = 0; i < n; ++i) {
                input_energy += (double)l[i] * l[i];
                output_energy += (double)ol[i] * ol[i];
                CHECK(or_[i] == 0.0f);
            }
        }
    }
    CHEQMeters meters;
    cheq_get_meters(eq, &meters);
    CHECK(meters.invalid_samples == 0 && meters.clipped_samples == 0);
    const double db = 10.0 * log10(output_energy / input_energy) + meters.headroom_db;
    cheq_destroy(eq);
    return db;
}

static void test_frequency_response(void) {
    const double bass_low = measure_response(6, 0, 0, 30, 48000);
    const double bass_corner = measure_response(6, 0, 0, 150, 48000);
    const double bass_high = measure_response(6, 0, 0, 10000, 48000);
    CHECK(fabs(bass_low - 6.0) < 0.05);
    CHECK(fabs(bass_corner - 3.0) < 0.02);
    CHECK(fabs(bass_high) < 0.01);

    const double mid_center = measure_response(0, -6, 0, 900, 48000);
    const double mid_low = measure_response(0, -6, 0, 30, 48000);
    const double mid_high = measure_response(0, -6, 0, 18000, 48000);
    CHECK(fabs(mid_center + 6.0) < 0.02);
    CHECK(fabs(mid_low) < 0.02);
    CHECK(fabs(mid_high) < 0.02);

    const double treble_corner = measure_response(0, 0, 6, 4500, 48000);
    const double treble_low = measure_response(0, 0, 6, 30, 48000);
    const double treble_high = measure_response(0, 0, 6, 18000, 48000);
    CHECK(fabs(treble_corner - 3.0) < 0.02);
    CHECK(fabs(treble_low) < 0.01);
    CHECK(fabs(treble_high - 6.0) < 0.01);

    const double low_rate_corner = measure_response(0, 0, -6, 3200, 8000);
    CHECK(fabs(low_rate_corner + 3.0) < 0.02);
    printf("Measured anchors, bass %.4f / %.4f dB, mid %.4f dB, treble %.4f / %.4f dB\n",
           bass_low, bass_corner, mid_center, treble_corner, treble_high);
    passed("shelf and peak frequency behavior including low sample rates");
}

static void test_stereo_isolation(void) {
    CHEQ *eq = cheq_create(48000);
    CHECK(eq);
    cheq_set_controls(eq, 6, -6, 6, 0, false);
    float l[1024] = {0}, r[1024] = {0}, ol[1024], or_[1024];
    l[0] = 0.7f;
    process_planar(eq, l, r, ol, or_, 1024);
    double left_energy = 0;
    for (size_t i = 0; i < 1024; ++i) {
        CHECK(or_[i] == 0);
        left_energy += fabs(ol[i]);
    }
    CHECK(left_energy > 0.1);
    cheq_destroy(eq);
    passed("independent filter history for stereo channels");
}

static void test_strides_and_input_preservation(void) {
    enum { N = 1027 };
    float l[N], r[N], plain_l[N], plain_r[N];
    float in_l[N * 3], in_r[N * 4], copy_l[N * 3], copy_r[N * 4];
    float out_l[N * 5], out_r[N * 6], interleaved[N * 2];
    for (size_t i = 0; i < N * 3; ++i) in_l[i] = -333.0f;
    for (size_t i = 0; i < N * 4; ++i) in_r[i] = -333.0f;
    for (size_t i = 0; i < N * 5; ++i) out_l[i] = -333.0f;
    for (size_t i = 0; i < N * 6; ++i) out_r[i] = -333.0f;
    for (size_t i = 0; i < N; ++i) {
        l[i] = in_l[i * 3] = interleaved[i * 2] = random_sample() * 0.3f;
        r[i] = in_r[i * 4] = interleaved[i * 2 + 1] = random_sample() * 0.3f;
    }
    memcpy(copy_l, in_l, sizeof(in_l));
    memcpy(copy_r, in_r, sizeof(in_r));
    CHEQ *reference = cheq_create(48000);
    CHEQ *strided = cheq_create(48000);
    CHEQ *in_place = cheq_create(48000);
    CHECK(reference && strided && in_place);
    cheq_set_controls(reference, -3, 2, -5, -2, false);
    cheq_set_controls(strided, -3, 2, -5, -2, false);
    cheq_set_controls(in_place, -3, 2, -5, -2, false);
    process_planar(reference, l, r, plain_l, plain_r, N);
    cheq_process_stereo(strided, in_l, 3, in_r, 4, out_l, 5, out_r, 6, N);
    cheq_process_stereo(in_place, interleaved, 2, interleaved + 1, 2,
                        interleaved, 2, interleaved + 1, 2, N);
    CHECK(memcmp(copy_l, in_l, sizeof(in_l)) == 0);
    CHECK(memcmp(copy_r, in_r, sizeof(in_r)) == 0);
    for (size_t i = 0; i < N; ++i) {
        CHECK(out_l[i * 5] == plain_l[i]);
        CHECK(out_r[i * 6] == plain_r[i]);
        CHECK(interleaved[i * 2] == plain_l[i]);
        CHECK(interleaved[i * 2 + 1] == plain_r[i]);
    }
    for (size_t i = 0; i < N * 5; ++i) {
        if (i % 5) CHECK(out_l[i] == -333.0f);
    }
    for (size_t i = 0; i < N * 6; ++i) {
        if (i % 6) CHECK(out_r[i] == -333.0f);
    }
    cheq_destroy(reference);
    cheq_destroy(strided);
    cheq_destroy(in_place);
    passed("planar, arbitrary stride, interleaved in place, and unchanged inputs");
}

static void test_partition_invariance(void) {
    enum { N = 9999 };
    float *l = malloc(sizeof(float) * N);
    float *r = malloc(sizeof(float) * N);
    float *a_l = malloc(sizeof(float) * N);
    float *a_r = malloc(sizeof(float) * N);
    float *b_l = malloc(sizeof(float) * N);
    float *b_r = malloc(sizeof(float) * N);
    CHECK(l && r && a_l && a_r && b_l && b_r);
    for (size_t i = 0; i < N; ++i) {
        l[i] = random_sample() * 0.25f;
        r[i] = random_sample() * 0.25f;
    }
    CHEQ *whole = cheq_create(44100), *pieces = cheq_create(44100);
    CHECK(whole && pieces);
    cheq_set_controls(whole, 5, -6, 1, -4, true);
    cheq_set_controls(pieces, 5, -6, 1, -4, true);
    process_planar(whole, l, r, a_l, a_r, N);
    for (size_t offset = 0; offset < N; offset += 37) {
        size_t n = N - offset > 37 ? 37 : N - offset;
        process_planar(pieces, l + offset, r + offset, b_l + offset, b_r + offset, n);
    }
    CHECK(memcmp(a_l, b_l, sizeof(float) * N) == 0);
    CHECK(memcmp(a_r, b_r, sizeof(float) * N) == 0);
    cheq_destroy(whole);
    cheq_destroy(pieces);
    free(l); free(r); free(a_l); free(a_r); free(b_l); free(b_r);
    passed("identical audio with different callback block sizes");
}

static void test_bad_samples_and_meters(void) {
    CHEQ *eq = cheq_create(48000);
    CHECK(eq);
    const float l[] = {NAN, INFINITY, -INFINITY, 2, -2, 0.5f, 0};
    const float r[] = {0, 0, 0, 0, 0, 0, 0};
    const float expected[] = {0, 0, 0, 1, -1, 0.5f, 0};
    float ol[7], or_[7];
    process_planar(eq, l, r, ol, or_, 7);
    for (size_t i = 0; i < 7; ++i) {
        CHECK(ol[i] == expected[i]);
        CHECK(or_[i] == 0.0f);
    }
    CHEQMeters meters;
    cheq_get_meters(eq, &meters);
    CHECK(meters.invalid_samples == 3);
    CHECK(meters.clipped_samples == 2);
    CHECK(meters.input_peak == 2 && meters.output_peak == 1);
    CHECK(cheq_clip_count(eq) == 2 && cheq_peak(eq) == 1);
    silence(eq, 48000);
    cheq_get_meters(eq, &meters);
    CHECK(meters.output_peak > 0 && meters.output_peak < 0.04f);
    CHECK(meters.clipped_samples == 2 && meters.invalid_samples == 3);
    cheq_destroy(eq);
    passed("invalid sample sanitation, counted final clamp, and meter decay");
}

static void test_control_sanitation(void) {
    CHEQ *clamped = cheq_create(48000), *reference = cheq_create(48000);
    CHECK(clamped && reference);
    cheq_set_controls(clamped, FLT_MAX, -FLT_MAX, 42, -200, false);
    cheq_set_controls(reference, 6, -6, 6, -12, false);
    float l[256], r[256], a_l[256], a_r[256], b_l[256], b_r[256];
    for (size_t i = 0; i < 256; ++i) {
        l[i] = random_sample() * 0.2f;
        r[i] = random_sample() * 0.2f;
    }
    for (size_t block = 0; block < 200; ++block) {
        process_planar(clamped, l, r, a_l, a_r, 256);
        process_planar(reference, l, r, b_l, b_r, 256);
        CHECK(memcmp(a_l, b_l, sizeof(a_l)) == 0);
        CHECK(memcmp(a_r, b_r, sizeof(a_r)) == 0);
    }
    cheq_destroy(clamped);
    cheq_destroy(reference);

    CHEQ *nonfinite = cheq_create(48000);
    CHECK(nonfinite);
    cheq_set_controls(nonfinite, NAN, INFINITY, -INFINITY, NAN, false);
    process_planar(nonfinite, l, r, a_l, a_r, 256);
    CHECK(memcmp(l, a_l, sizeof(l)) == 0);
    CHECK(memcmp(r, a_r, sizeof(r)) == 0);
    cheq_destroy(nonfinite);
    passed("bounded tone and trim controls and nonfinite control values");
}

static void test_bypass_and_trim(void) {
    CHEQ *eq = cheq_create(48000);
    CHECK(eq);
    cheq_set_controls(eq, 6, -4, 3, -6, true);
    silence(eq, 96000);
    float l[256], r[256], ol[256], or_[256];
    for (size_t i = 0; i < 256; ++i) {
        l[i] = random_sample() * 0.3f;
        r[i] = random_sample() * 0.3f;
    }
    process_planar(eq, l, r, ol, or_, 256);
    CHEQMeters meters;
    cheq_get_meters(eq, &meters);
    const double gain = pow(10, (-6.0 - meters.headroom_db) / 20.0);
    CHECK(meters.headroom_db >= 6.9f);
    for (size_t i = 0; i < 256; ++i) {
        CHECK(fabs(ol[i] - l[i] * gain) < 1e-7);
        CHECK(fabs(or_[i] - r[i] * gain) < 1e-7);
    }
    cheq_destroy(eq);
    passed("bypass removes tone while retaining trim and comparison headroom");
}

static void test_headroom_and_transitions(void) {
    const double frequencies[] = {30, 150, 900, 4500, 18000};
    double maximum = 0;
    for (size_t f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
        CHEQ *eq = cheq_create(48000);
        CHECK(eq);
        float l[240], r[240], ol[240], or_[240];
        for (size_t block = 0; block < 400; ++block) {
            if (block == 80) cheq_set_controls(eq, 6, 6, 6, 0, false);
            if (block == 240) cheq_set_controls(eq, -6, -6, -6, 0, false);
            if (block == 320) cheq_set_controls(eq, 0, 0, 0, 0, true);
            for (size_t i = 0; i < 240; ++i) {
                const size_t n = block * 240 + i;
                l[i] = (float)(0.99 * sin(2 * PI * frequencies[f] * n / 48000.0));
                r[i] = -l[i];
            }
            process_planar(eq, l, r, ol, or_, 240);
            for (size_t i = 0; i < 240; ++i) {
                CHECK(isfinite(ol[i]) && isfinite(or_[i]));
                maximum = fmax(maximum, fabs(ol[i]));
            }
        }
        CHECK(cheq_clip_count(eq) == 0);
        cheq_destroy(eq);
    }
    printf("Maximum sine sample through extreme setting transitions: %.8f\n", maximum);
    passed("automatic headroom avoids sample clipping on near full scale test tones");
}

static void test_smooth_rapid_updates(void) {
    CHEQ *eq = cheq_create(48000);
    CHECK(eq);
    float l[480], r[480], ol[480], or_[480];
    for (size_t i = 0; i < 480; ++i) l[i] = r[i] = 0.25f;
    process_planar(eq, l, r, ol, or_, 480);
    double previous = ol[479], maximum_step = 0;
    for (size_t update = 0; update < 400; ++update) {
        const float sign = update % 2 ? 1.0f : -1.0f;
        cheq_set_controls(eq, 6 * sign, -6 * sign, 6 * sign,
                           update % 3 ? -12 : 0, update % 4 < 2);
        process_planar(eq, l, r, ol, or_, 480);
        for (size_t i = 0; i < 480; ++i) {
            CHECK(isfinite(ol[i]) && fabs(ol[i]) <= 1.0);
            maximum_step = fmax(maximum_step, fabs(ol[i] - previous));
            previous = ol[i];
            CHECK(ol[i] == or_[i]);
        }
    }
    CHECK(maximum_step < 0.004);
    CHECK(cheq_clip_count(eq) == 0);
    printf("Maximum adjacent sample change on DC during rapid updates: %.8f\n", maximum_step);
    cheq_destroy(eq);
    passed("smooth tone, trim, and bypass transitions under rapid updates");
}

static void test_extreme_rates_and_controls(void) {
    const double rates[] = {8000, 11025, 44100, 48000, 96000, 192000, 384000};
    float l[257], r[257], ol[257], or_[257];
    for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        CHEQ *eq = cheq_create(rates[rate]);
        CHECK(eq);
        for (size_t configuration = 0; configuration < 32; ++configuration) {
            cheq_set_controls(eq,
                configuration & 1 ? 6 : -6,
                configuration & 2 ? 6 : -6,
                configuration & 4 ? 6 : -6,
                configuration & 8 ? -12 : 0,
                configuration & 16);
            for (size_t block = 0; block < 20; ++block) {
                for (size_t i = 0; i < 257; ++i) {
                    l[i] = random_sample() * 0.75f;
                    r[i] = random_sample() * 0.75f;
                }
                process_planar(eq, l, r, ol, or_, 257);
                for (size_t i = 0; i < 257; ++i) {
                    CHECK(isfinite(ol[i]) && fabsf(ol[i]) <= 1.0f);
                    CHECK(isfinite(or_[i]) && fabsf(or_[i]) <= 1.0f);
                }
            }
        }
        CHEQMeters meters;
        cheq_get_meters(eq, &meters);
        CHECK(meters.invalid_samples == 0);
        CHECK(meters.clipped_samples == 0);
        cheq_destroy(eq);
    }
    passed("finite output across all control extremes at seven sample rates");
}

/* Boundary behavior is part of the clamp contract, including counts per
   channel rather than counts per frame. */
static void test_clip_boundaries(void) {
    CHEQ *eq = cheq_create(48000);
    CHECK(eq);
    const float above = nextafterf(1.0f, INFINITY);
    const float below = nextafterf(1.0f, 0.0f);
    const float left[] = {1, -1, below, -below, above, -above, FLT_MAX, -FLT_MAX};
    const float right[] = {-1, 1, -below, below, -above, above, -FLT_MAX, FLT_MAX};
    float out_left[8], out_right[8];
    process_planar(eq, left, right, out_left, out_right, 8);
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(out_left[i] == fmaxf(-1, fminf(1, left[i])));
        CHECK(out_right[i] == -out_left[i]);
    }
    CHEQMeters meters;
    cheq_get_meters(eq, &meters);
    CHECK(meters.clipped_samples == 8);
    CHECK(meters.invalid_samples == 0);
    CHECK(meters.input_peak == FLT_MAX && meters.output_peak == 1);
    silence(eq, 256);
    printf("Post-extreme-input clips: %llu\n", (unsigned long long)cheq_clip_count(eq));
    CHECK(cheq_clip_count(eq) == 8);
    cheq_destroy(eq);
    passed("exact clamp boundaries and per-channel counts for finite extreme input");
}

static void test_bypass_rates_and_cut_headroom(void) {
    const double rates[] = {8000, 44100, 48000, 96000, 192000, 384000};
    const float tones[][3] = {{0, 0, 0}, {-6, -6, -6}, {6, 6, 6}};
    float left[129], right[129], out_left[129], out_right[129];
    for (unsigned i = 0; i < 129; ++i) {
        left[i] = i % 2 ? -0.75f : 0.75f;
        right[i] = (float)i / 129.0f;
    }
    for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        for (unsigned setting = 0; setting < 3; ++setting) {
            CHEQ *eq = cheq_create(rates[rate]);
            CHECK(eq);
            cheq_set_controls(eq, tones[setting][0], tones[setting][1],
                               tones[setting][2], -12, true);
            silence(eq, (size_t)(rates[rate] * 3));
            process_planar(eq, left, right, out_left, out_right, 129);
            CHEQMeters meters;
            cheq_get_meters(eq, &meters);
            CHECK(isfinite(meters.headroom_db) && meters.headroom_db >= 0);
            if (setting == 0) CHECK(meters.headroom_db == 0);
            if (setting == 1) CHECK(meters.headroom_db >= 0.99f && meters.headroom_db <= 1.01f);
            if (setting == 2) CHECK(meters.headroom_db > 7 && meters.headroom_db < 19.1f);
            const double gain = pow(10, (-12.0 - meters.headroom_db) / 20.0);
            for (unsigned i = 0; i < 129; ++i) {
                CHECK(fabs(out_left[i] - left[i] * gain) < 1e-7);
                CHECK(fabs(out_right[i] - right[i] * gain) < 1e-7);
            }
            CHECK(meters.clipped_samples == 0 && meters.invalid_samples == 0);
            cheq_destroy(eq);
        }
    }
    passed("settled bypass fidelity with trim, flat, cuts, and boosts at six rates");
}

static void test_bypass_keeps_filter_history(void) {
    CHEQ *wet = cheq_create(48000), *comparison = cheq_create(48000);
    CHECK(wet && comparison);
    cheq_set_controls(wet, 6, -4, 5, -3, false);
    cheq_set_controls(comparison, 6, -4, 5, -3, true);
    float left[127], right[127], wet_l[127], wet_r[127], test_l[127], test_r[127];
    double final_difference = 0;
    for (unsigned block = 0; block < 1400; ++block) {
        if (block == 500) cheq_set_controls(comparison, 6, -4, 5, -3, false);
        for (unsigned i = 0; i < 127; ++i) {
            const unsigned n = block * 127 + i;
            left[i] = (float)(0.2 * sin(2 * PI * 33 * n / 48000.0));
            right[i] = (float)(0.3 * sin(2 * PI * 1103 * n / 48000.0));
        }
        process_planar(wet, left, right, wet_l, wet_r, 127);
        process_planar(comparison, left, right, test_l, test_r, 127);
        if (block >= 1000) {
            for (unsigned i = 0; i < 127; ++i) {
                final_difference = fmax(final_difference, fabs(wet_l[i] - test_l[i]));
                final_difference = fmax(final_difference, fabs(wet_r[i] - test_r[i]));
            }
        }
    }
    CHECK(final_difference < 1e-7);
    CHECK(cheq_clip_count(wet) == 0 && cheq_clip_count(comparison) == 0);
    cheq_destroy(wet); cheq_destroy(comparison);
    passed("return from bypass matches continuously maintained wet filter history");
}

typedef struct {
    CHEQ *eq;
    atomic_bool start;
    atomic_bool audio_done;
    atomic_bool failed;
    atomic_uint writes;
    atomic_uint reads;
} SharedTest;

static void *control_writer(void *context) {
    SharedTest *shared = context;
    while (!atomic_load(&shared->start)) { }
    for (unsigned i = 0; i < 1000; ++i) {
        const float tone = (float)((int)(i % 1201) - 600) / 100.0f;
        cheq_set_controls(shared->eq, tone, -tone, tone / 2,
                           -(float)(i % 1201) / 100.0f, i % 2);
        atomic_fetch_add(&shared->writes, 1);
    }
    return NULL;
}

static void *meter_reader(void *context) {
    SharedTest *shared = context;
    while (!atomic_load(&shared->start)) { }
    uint64_t last_clips = 0, last_invalid = 0;
    do {
        CHEQMeters meters;
        cheq_get_meters(shared->eq, &meters);
        if (!isfinite(meters.input_peak) || !isfinite(meters.output_peak)
            || !isfinite(meters.headroom_db) || meters.output_peak < 0
            || meters.output_peak > 1 || meters.clipped_samples < last_clips
            || meters.invalid_samples < last_invalid) {
            atomic_store(&shared->failed, true);
        }
        last_clips = meters.clipped_samples;
        last_invalid = meters.invalid_samples;
        atomic_fetch_add(&shared->reads, 1);
    } while (!atomic_load(&shared->audio_done));
    return NULL;
}

static void test_concurrent_controls_and_meters(void) {
    SharedTest shared;
    shared.eq = cheq_create(48000);
    CHECK(shared.eq);
    atomic_init(&shared.start, false);
    atomic_init(&shared.audio_done, false);
    atomic_init(&shared.failed, false);
    atomic_init(&shared.writes, 0);
    atomic_init(&shared.reads, 0);
    pthread_t writer, reader;
    CHECK(pthread_create(&writer, NULL, control_writer, &shared) == 0);
    CHECK(pthread_create(&reader, NULL, meter_reader, &shared) == 0);
    atomic_store(&shared.start, true);
    float l[128], r[128], ol[128], or_[128];
    for (size_t i = 0; i < 128; ++i) {
        l[i] = random_sample() * 0.25f;
        r[i] = random_sample() * 0.25f;
    }
    for (unsigned block = 0; block < 6000; ++block) {
        process_planar(shared.eq, l, r, ol, or_, 128);
        for (size_t i = 0; i < 128; ++i) {
            CHECK(isfinite(ol[i]) && fabsf(ol[i]) <= 1.0f);
            CHECK(isfinite(or_[i]) && fabsf(or_[i]) <= 1.0f);
        }
    }
    atomic_store(&shared.audio_done, true);
    CHECK(pthread_join(writer, NULL) == 0);
    CHECK(pthread_join(reader, NULL) == 0);
    CHECK(atomic_load(&shared.writes) == 1000);
    CHECK(atomic_load(&shared.reads) > 0);
    CHECK(!atomic_load(&shared.failed));
    CHECK(cheq_clip_count(shared.eq) == 0);
    cheq_destroy(shared.eq);
    passed("concurrent control publication, audio processing, and meter reads");
}

int main(void) {
    test_create_and_nulls();
    test_flat_identity();
    test_frequency_response();
    test_stereo_isolation();
    test_strides_and_input_preservation();
    test_partition_invariance();
    test_bad_samples_and_meters();
    test_control_sanitation();
    test_bypass_and_trim();
    test_headroom_and_transitions();
    test_smooth_rapid_updates();
    test_extreme_rates_and_controls();
    test_clip_boundaries();
    test_bypass_rates_and_cut_headroom();
    test_bypass_keeps_filter_history();
    test_concurrent_controls_and_meters();
    printf("All %u EQ test groups passed.\n", completed_tests);
    return 0;
}
