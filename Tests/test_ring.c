#include "../Sources/CHRing.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static void test_basic(void) {
    assert(!chring_create(63, 8));
    assert(!chring_create(64, 32));
    CHRing *r = chring_create(256, 64);
    assert(r);
    float in[256], out[128];
    for (size_t i = 0; i < 128; ++i) { in[2*i] = (float)i / 256; in[2*i+1] = -(float)i / 256; }
    assert(chring_read_stereo(r, out, 2, out+1, 2, 64) == 0);
    for (size_t i = 0; i < 128; ++i) assert(out[i] == 0);
    assert(chring_underruns(r) == 0);
    assert(chring_write_stereo(r, in, 2, in+1, 2, 128) == 128);
    assert(chring_read_stereo(r, out, 2, out+1, 2, 64) == 64);
    for (size_t i = 0; i < 128; ++i) assert(out[i] == in[i]);
    assert(chring_available(r) == 64);
    assert(chring_write_stereo(r, in, 2, in+1, 2, 128) == 128);
    assert(chring_write_stereo(r, in, 2, in+1, 2, 128) == 64);
    assert(chring_overruns(r) == 64);
    chring_reset_consumer(r);
    assert(chring_available(r) == 0);
    assert(chring_read_stereo(r, out, 2, out+1, 2, 64) == 0);
    chring_destroy(r);
}

static void test_wrap_and_stereo(void) {
    CHRing *r = chring_create(1024, 128);
    assert(r);
    float inL[192], inR[192], outL[64], outR[64];
    for (size_t i = 0; i < 192; ++i) { inL[i] = (float)i; inR[i] = 0; }
    assert(chring_write_stereo(r, inL, 1, inR, 1, 192) == 192);
    size_t position = 0;
    for (size_t block = 0; block < 10000; ++block) {
        assert(chring_read_stereo(r, outL, 1, outR, 1, 64) == 64);
        for (size_t i = 0; i < 64; ++i) { assert(outL[i] == (float)(position+i)); assert(outR[i] == 0); }
        position += 64;
        for (size_t i = 0; i < 64; ++i) inL[i] = (float)(position+128+i);
        assert(chring_write_stereo(r, inL, 1, inR, 1, 64) == 64);
    }
    assert(chring_underruns(r) == 0);
    assert(chring_overruns(r) == 0);
    chring_destroy(r);
}

static void test_silence_and_underrun(void) {
    CHRing *r = chring_create(256, 16);
    float in[64], out[64];
    for (size_t i = 0; i < 64; ++i) in[i] = NAN;
    in[3] = INFINITY;
    assert(chring_write_stereo(r, in, 1, in, 1, 64) == 64);
    assert(chring_read_stereo(r, out, 1, out, 1, 32) == 32);
    for (size_t i = 0; i < 32; ++i) assert(out[i] == 0);
    assert(chring_read_stereo(r, out, 1, out, 1, 32) < 32);
    for (size_t i = 0; i < 32; ++i) assert(out[i] == 0);
    assert(chring_underruns(r) == 1);
    chring_destroy(r);
}

static void test_small_clock_drift(int direction) {
    CHRing *r = chring_create(8192, 512);
    float inL[641], inR[641], outL[128], outR[128];
    for (size_t i = 0; i < 641; ++i) { inL[i] = 0.25f; inR[i] = -0.5f; }
    assert(chring_write_stereo(r, inL, 1, inR, 1, 640) == 640);
    /* One changed input frame per 64 blocks is approximately122 ppm. */
    for (size_t block = 0; block < 30000; ++block) {
        assert(chring_read_stereo(r, outL, 1, outR, 1, 128) == 128);
        for (size_t i = 0; i < 128; ++i) { assert(fabsf(outL[i] - .25f) < 1e-6f); assert(fabsf(outR[i] + .5f) < 1e-6f); }
        size_t count = (size_t)(128 + ((block % 64 == 0) ? direction : 0));
        assert(chring_write_stereo(r, inL, 1, inR, 1, count) == count);
    }
    assert(chring_underruns(r) == 0);
    assert(chring_overruns(r) == 0);
    assert(chring_available(r) > 256 && chring_available(r) < 1536);
    chring_destroy(r);
}

static void test_large_callback_with_slow_input_clock(void) {
    /* A render callback can grow after choosing a 1024 frame target. A
       deadband based only on this 4096 frame callback previously consumed
       the entire backlog and caused an underrun at callback 2047. */
    CHRing *r = chring_create(16384, 1024);
    assert(r);
    float inL[5120], inR[5120], outL[4096], outR[4096];
    for (size_t i = 0; i < 5120; ++i) { inL[i] = 0.25f; inR[i] = -0.5f; }
    assert(chring_write_stereo(r, inL, 1, inR, 1, 5120) == 5120);
    for (size_t block = 0; block < 6000; ++block) {
        assert(chring_read_stereo(r, outL, 1, outR, 1, 4096) == 4096);
        for (size_t i = 0; i < 4096; ++i) {
            assert(fabsf(outL[i] - 0.25f) < 1e-6f);
            assert(fabsf(outR[i] + 0.5f) < 1e-6f);
        }
        /* One missing frame every two blocks is about 122 ppm. */
        const size_t count = block % 2 == 0 ? 4095 : 4096;
        assert(chring_write_stereo(r, inL, 1, inR, 1, count) == count);
    }
    assert(chring_underruns(r) == 0);
    assert(chring_overruns(r) == 0);
    assert(chring_available(r) > 4096 + 256);
    assert(chring_available(r) < 4096 + 1024);
    chring_destroy(r);
}

typedef struct { CHRing *ring; _Atomic int done; } ThreadContext;
static void *producer(void *opaque) {
    ThreadContext *context = opaque;
    float left[64], right[64];
    size_t next = 0;
    for (size_t block = 0; block < 12000; ++block) {
        while (chring_available(context->ring) > 640) sched_yield();
        for (size_t i = 0; i < 64; ++i) { left[i] = (float)(next+i) * .000001f; right[i] = -left[i]; }
        assert(chring_write_stereo(context->ring, left, 1, right, 1, 64) == 64);
        next += 64;
    }
    atomic_store_explicit(&context->done, 1, memory_order_release);
    return NULL;
}
static void test_concurrency(void) {
    ThreadContext context = { .ring = chring_create(2048, 128) };
    atomic_init(&context.done, 0);
    assert(context.ring);
    pthread_t writer;
    assert(pthread_create(&writer, NULL, producer, &context) == 0);
    float left[64], right[64], previous = -1;
    size_t heard = 0;
    for (;;) {
        if (chring_available(context.ring) < 256) {
            if (atomic_load_explicit(&context.done, memory_order_acquire)) break;
            sched_yield(); continue;
        }
        size_t count = chring_read_stereo(context.ring, left, 1, right, 1, 64);
        for (size_t i = 0; i < count; ++i) {
            assert(isfinite(left[i]) && left[i] >= previous);
            assert(fabsf(left[i] + right[i]) < 1e-7f);
            previous = left[i]; ++heard;
        }
    }
    assert(pthread_join(writer, NULL) == 0);
    assert(heard > 700000);
    assert(chring_overruns(context.ring) == 0);
    assert(chring_underruns(context.ring) == 0);
    chring_destroy(context.ring);
}

int main(void) {
    test_basic();
    test_wrap_and_stereo();
    test_silence_and_underrun();
    test_small_clock_drift(1);
    test_small_clock_drift(-1);
    test_large_callback_with_slow_input_clock();
    test_concurrency();
    puts("7 ring buffer test groups passed");
    return 0;
}
