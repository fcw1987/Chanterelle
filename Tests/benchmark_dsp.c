/* Synthetic DSP timing only. Does not open hardware, capture or play audio. */
#define _POSIX_C_SOURCE 200809L
#include "CHEQ.h"
#include "CHRing.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { BLOCKS = 6000, MAX_FRAMES = 1024 };
static volatile float result_sink;
static double seconds(struct timespec value) {
    return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
}
static double wall_time(void) {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return seconds(t);
}
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void benchmark(size_t frames, bool bypass) {
    CHEQ *eq = cheq_create(48000);
    CHRing *ring = chring_create(8192, 1024);
    assert(eq && ring);
    cheq_set_controls(eq, 6, -4, 3, -3, bypass);
    float left[MAX_FRAMES], right[MAX_FRAMES], out_l[MAX_FRAMES], out_r[MAX_FRAMES];
    for (unsigned i = 0; i < MAX_FRAMES; ++i) {
        left[i] = (float)(0.25 * sin(i * 0.31));
        right[i] = (float)(0.25 * cos(i * 0.17));
    }
    assert(chring_write_stereo(ring, left, 1, right, 1, MAX_FRAMES) == MAX_FRAMES);
    /* Exclude initial priming and filter smoothing from steady-state timing. */
    for (unsigned b = 0; b < 500; ++b) {
        assert(chring_write_stereo(ring, left, 1, right, 1, frames) == frames);
        assert(chring_read_stereo(ring, out_l, 1, out_r, 1, frames) == frames);
        cheq_process_stereo(eq, out_l, 1, out_r, 1, out_l, 1, out_r, 1, frames);
    }
    double durations[BLOCKS];
    const double begin = wall_time();
    const clock_t cpu_begin = clock();
    for (unsigned b = 0; b < BLOCKS; ++b) {
        const double block_begin = wall_time();
        assert(chring_write_stereo(ring, left, 1, right, 1, frames) == frames);
        assert(chring_read_stereo(ring, out_l, 1, out_r, 1, frames) == frames);
        cheq_process_stereo(eq, out_l, 1, out_r, 1, out_l, 1, out_r, 1, frames);
        durations[b] = wall_time() - block_begin;
        result_sink = out_l[b % frames];
    }
    const double cpu = (double)(clock() - cpu_begin) / CLOCKS_PER_SEC;
    const double elapsed = wall_time() - begin;
    qsort(durations, BLOCKS, sizeof(durations[0]), compare);
    const double audio_seconds = (double)frames * BLOCKS / 48000;
    printf("DSP frames=%zu bypass=%s blocks=%u audio_seconds=%.3f wall_seconds=%.6f "
           "process_cpu_seconds=%.6f cpu_percent_of_realtime=%.3f "
           "block_p50_us=%.3f block_p95_us=%.3f block_max_us=%.3f "
           "callback_budget_us=%.3f clips=%llu underruns=%llu overruns=%llu\n",
           frames, bypass ? "true" : "false", BLOCKS, audio_seconds, elapsed,
           cpu, cpu / audio_seconds * 100, durations[BLOCKS / 2] * 1e6,
           durations[BLOCKS * 95 / 100] * 1e6, durations[BLOCKS - 1] * 1e6,
           frames / 48000.0 * 1e6, (unsigned long long)cheq_clip_count(eq),
           (unsigned long long)chring_underruns(ring),
           (unsigned long long)chring_overruns(ring));
    assert(cheq_clip_count(eq) == 0 && chring_underruns(ring) == 0 && chring_overruns(ring) == 0);
    cheq_destroy(eq); chring_destroy(ring);
}
int main(void) {
    const size_t frames[] = {64, 256, 1024};
    puts("Synthetic single-thread EQ + ring CPU benchmark; excludes Apple audio APIs, device latency and UI.");
    for (unsigned i = 0; i < 3; ++i) {
        benchmark(frames[i], false);
        benchmark(frames[i], true);
    }
    return 0;
}
