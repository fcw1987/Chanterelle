#ifndef CHEQ_H
#define CHEQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CHEQ CHEQ;

typedef struct {
    /* Linear amplitude, with a 300 ms peak decay. Input can exceed 1. */
    float input_peak;
    float output_peak;
    /* Current automatic attenuation, expressed as a positive dB value. */
    float headroom_db;
    /* Individual output samples that required the final safety clamp. */
    uint64_t clipped_samples;
    /* Nonfinite input or arithmetic results replaced with silence. */
    uint64_t invalid_samples;
} CHEQMeters;

/*
 * Creates a stereo equalizer for an immutable sample rate in [8000, 384000].
 * Returns NULL for an unsupported rate, allocation failure, or a platform
 * without the lock free atomics needed by its audio callback.
 *
 * The initial state is flat, 0 dB trim, and tone processing enabled.
 * Recreate the object while audio is stopped when the sample rate changes.
 */
CHEQ *cheq_create(double sampleRate);

/* The caller must stop all processing, setters, and readers before destroying. */
void cheq_destroy(CHEQ *eq);

/*
 * Thread safe control publication. Call from a control thread, not an audio
 * callback, since this function evaluates the filter frequency response.
 * Multiple setters are safe, with the last completed publication winning.
 *
 * Bass is a 150 Hz low shelf. Mid is a 900 Hz peak with Q = 0.8. Treble
 * is a 4500 Hz high shelf. Shelf slope S = 1. At low sample rates, a
 * corner above 0.4 * sampleRate is moved down to that frequency.
 *
 * Tone gains are clamped to [-6, 6] dB and trim to [-12, 0] dB, with
 * 0.01 dB resolution. Nonfinite control values become 0 dB.
 * Bypass smoothly removes the tone filters, retaining output trim and
 * the headroom attenuation associated with the selected tone settings.
 * Filter history stays active during bypass for a smooth return.
 *
 * Automatic headroom samples the combined frequency response, offsets any
 * estimated boost, and adds 1 dB for a nonflat curve. It reduces overload
 * risk but is not a bound on arbitrary waveform peaks, a lookahead limiter,
 * or intersample peak protection. A final sample clamp prevents values
 * outside [-1, 1]. Clamp events are reported by the meters.
 */
void cheq_set_controls(CHEQ *eq, float bass, float mid, float treble,
                      float trim, bool bypass);

/*
 * Process stereo Float32 audio. Strides are in floats, not bytes.
 * Planar stereo uses strides of 1. Interleaved stereo uses channel pointers
 * input and input + 1, output and output + 1, with strides of 2.
 *
 * No allocations, locks, waits, or operating system calls occur here.
 * Exactly one audio thread may process an object at a time. Controls and
 * meter reads can run concurrently. The input is only read. Exact in place
 * operation is supported if the caller intentionally aliases each output
 * with its corresponding input. Other overlapping buffer layouts are not
 * supported. Read both inputs before writing either output.
 *
 * Null pointers or zero strides cause an immediate return. A zero frame
 * count is always a no op. Buffers must contain the requested frame count.
 * Nonfinite samples become zero before reaching the filters.
 */
void cheq_process_stereo(CHEQ *eq,
                        const float *inL, size_t inLStride,
                        const float *inR, size_t inRStride,
                        float *outL, size_t outLStride,
                        float *outR, size_t outRStride,
                        size_t frames);

/*
 * Nonblocking, individually atomic meter reads. Fields can refer to adjacent
 * audio blocks. Callers do not have to synchronize with audio processing.
 * A null object produces zero values. A null destination is ignored.
 */
void cheq_get_meters(const CHEQ *eq, CHEQMeters *meters);

/* Convenience accessors for the combined output peak and safety clamp count. */
float cheq_peak(const CHEQ *eq);
uint64_t cheq_clip_count(const CHEQ *eq);

#ifdef __cplusplus
}
#endif

#endif
