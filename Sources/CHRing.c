#include "CHRing.h"
#include <math.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>

struct CHRing {
    float *left;
    float *right;
    size_t capacity;
    size_t mask;
    size_t target;
    _Atomic uint64_t read_index;
    _Atomic uint64_t write_index;
    _Atomic uint64_t underruns;
    _Atomic uint64_t overruns;
    /* The consumer exclusively owns the fields below. */
    double fraction;
    double correction;
    bool primed;
};

static void silence(float *left, size_t ls, float *right, size_t rs, size_t frames) {
    if (!left || !right || !ls || !rs) return;
    for (size_t i = 0; i < frames; ++i) { left[i * ls] = 0; right[i * rs] = 0; }
}

CHRing *chring_create(size_t capacity, size_t target) {
    if (capacity < 16 || capacity > (1u << 20) || (capacity & (capacity - 1)) ||
        target < 4 || target >= capacity / 2) return NULL;
    CHRing *ring = calloc(1, sizeof(*ring));
    if (!ring) return NULL;
    atomic_init(&ring->read_index, 0);
    atomic_init(&ring->write_index, 0);
    atomic_init(&ring->underruns, 0);
    atomic_init(&ring->overruns, 0);
    if (!atomic_is_lock_free(&ring->read_index) || !atomic_is_lock_free(&ring->write_index) ||
        !atomic_is_lock_free(&ring->underruns) || !atomic_is_lock_free(&ring->overruns)) {
        free(ring); return NULL;
    }
    ring->left = calloc(capacity, sizeof(float));
    ring->right = calloc(capacity, sizeof(float));
    if (!ring->left || !ring->right) { chring_destroy(ring); return NULL; }
    ring->capacity = capacity;
    ring->mask = capacity - 1;
    ring->target = target;
    return ring;
}

void chring_destroy(CHRing *ring) {
    if (!ring) return;
    free(ring->left); free(ring->right); free(ring);
}

size_t chring_write_stereo(CHRing *ring, const float *left, size_t ls,
                           const float *right, size_t rs, size_t frames) {
    if (!ring || !left || !right || !ls || !rs) return 0;
    uint64_t write = atomic_load_explicit(&ring->write_index, memory_order_relaxed);
    uint64_t read = atomic_load_explicit(&ring->read_index, memory_order_acquire);
    uint64_t used = write - read;
    if (used > ring->capacity) return 0;
    size_t room = ring->capacity - (size_t)used;
    size_t accepted = frames < room ? frames : room;
    for (size_t i = 0; i < accepted; ++i) {
        size_t at = (size_t)(write + i) & ring->mask;
        float l = left[i * ls], r = right[i * rs];
        ring->left[at] = isfinite(l) ? l : 0;
        ring->right[at] = isfinite(r) ? r : 0;
    }
    atomic_store_explicit(&ring->write_index, write + accepted, memory_order_release);
    if (accepted < frames) atomic_fetch_add_explicit(&ring->overruns, frames - accepted, memory_order_relaxed);
    return accepted;
}

size_t chring_read_stereo(CHRing *ring, float *left, size_t ls,
                          float *right, size_t rs, size_t frames) {
    if (!left || !right || !ls || !rs) return 0;
    if (!ring || frames > ring->capacity - ring->target - 2) {
        silence(left, ls, right, rs, frames); return 0;
    }
    if (!frames) return 0;
    uint64_t read = atomic_load_explicit(&ring->read_index, memory_order_relaxed);
    uint64_t write = atomic_load_explicit(&ring->write_index, memory_order_acquire);
    uint64_t available = write - read;
    if (!ring->primed) {
        if (available < ring->target + frames) { silence(left, ls, right, rs, frames); return 0; }
        ring->primed = true;
        ring->fraction = 0;
        ring->correction = 0;
    }

    /* Occupancy is sampled before rendering a block, so include that block in
       the target. A deadband avoids chasing ordinary callback phase jitter.
       The maximum rate correction is 500 parts per million, not a general
       purpose resampler. The smooth control prevents sudden pitch changes. */
    double error = (double)available - (double)(ring->target + frames);
    /* A large render slice can exceed the target backlog. Keep enough backlog
       outside the deadband for negative clock drift to be corrected before
       reaching the interpolation lookahead boundary. */
    double deadband = fmin((double)ring->target * 0.25,
                           fmax(16.0, (double)frames * 0.5));
    if (fabs(error) <= deadband) error = 0;
    else error -= copysign(deadband, error);
    double desired = error / (double)ring->target * 0.001;
    desired = fmax(-0.0005, fmin(0.0005, desired));
    ring->correction += 0.02 * (desired - ring->correction);
    double increment = 1.0 + ring->correction;
    size_t produced = 0;
    for (; produced < frames; ++produced) {
        unsigned step = (unsigned)floor(ring->fraction + increment);
        if (write - read <= (step > 1 ? step : 1)) {
            /* Give a concurrent capture callback one fresh snapshot. */
            write = atomic_load_explicit(&ring->write_index, memory_order_acquire);
            if (write - read <= (step > 1 ? step : 1)) break;
        }
        size_t first = (size_t)read & ring->mask;
        size_t second = (size_t)(read + 1) & ring->mask;
        double blend = ring->fraction;
        left[produced * ls] = (float)((1.0 - blend) * ring->left[first] + blend * ring->left[second]);
        right[produced * rs] = (float)((1.0 - blend) * ring->right[first] + blend * ring->right[second]);
        ring->fraction += increment;
        step = (unsigned)floor(ring->fraction);
        ring->fraction -= step;
        read += step;
    }
    if (produced < frames) {
        silence(left + produced * ls, ls, right + produced * rs, rs, frames - produced);
        atomic_fetch_add_explicit(&ring->underruns, 1, memory_order_relaxed);
        /* Discard the last isolated sample and rebuffer a complete block. */
        read = atomic_load_explicit(&ring->write_index, memory_order_acquire);
        ring->fraction = 0;
        ring->correction = 0;
        ring->primed = false;
    }
    atomic_store_explicit(&ring->read_index, read, memory_order_release);
    return produced;
}

void chring_reset_consumer(CHRing *ring) {
    if (!ring) return;
    uint64_t write = atomic_load_explicit(&ring->write_index, memory_order_acquire);
    atomic_store_explicit(&ring->read_index, write, memory_order_release);
    ring->fraction = 0;
    ring->correction = 0;
    ring->primed = false;
}

size_t chring_available(const CHRing *ring) {
    if (!ring) return 0;
    uint64_t read = atomic_load_explicit(&ring->read_index, memory_order_acquire);
    uint64_t write = atomic_load_explicit(&ring->write_index, memory_order_acquire);
    uint64_t count = write - read;
    return count <= ring->capacity ? (size_t)count : ring->capacity;
}
uint64_t chring_underruns(const CHRing *ring) {
    return ring ? atomic_load_explicit(&ring->underruns, memory_order_relaxed) : 0;
}
uint64_t chring_overruns(const CHRing *ring) {
    return ring ? atomic_load_explicit(&ring->overruns, memory_order_relaxed) : 0;
}
