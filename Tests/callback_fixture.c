/* Portable type and buffer fixtures. The test runner inserts the current
   callback implementation from Sources/CHAudioEngine.m at the marker below.
   These fixtures validate our C logic, not Apple framework compatibility. */
#include "CHEQ.h"
#include "CHRing.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
typedef int BOOL;
#define YES 1
#define NO 0
#define noErr 0
typedef uint32_t UInt32;
typedef uint32_t AudioObjectID;
typedef int32_t OSStatus;
typedef uint32_t AudioUnitRenderActionFlags;
typedef struct { double sampleTime; } AudioTimeStamp;
typedef struct { UInt32 mNumberChannels; UInt32 mDataByteSize; void *mData; } AudioBuffer;
typedef struct { UInt32 mNumberBuffers; AudioBuffer mBuffers[]; } AudioBufferList;

/* CHAUDIO_CALLBACKS */

static AudioBufferList *buffers(unsigned count) {
    AudioBufferList *b = calloc(1, sizeof(*b) + count * sizeof(AudioBuffer));
    assert(b); b->mNumberBuffers = count; return b;
}
static void uf_test_init_state(CHRenderState *s) {
    memset(s, 0, sizeof(*s));
    s->eq = cheq_create(48000); s->ring = chring_create(8192, 128);
    assert(s->eq && s->ring); s->maximumOutputFrames = 4096;
    atomic_init(&s->alive, true); atomic_init(&s->renderEnabled, false);
    atomic_init(&s->outputReady, false); atomic_init(&s->fault, 0);
    atomic_init(&s->captureCallbacks, 0); atomic_init(&s->outputCallbacks, 0);
    atomic_init(&s->signalBlocks, 0);
}
static void uf_test_destroy_state(CHRenderState *s) {
    chring_destroy(s->ring); cheq_destroy(s->eq);
}
typedef struct { CHRenderState *state; atomic_bool done; } CallbackThreads;
static void *uf_test_capture_thread(void *opaque) {
    CallbackThreads *shared = opaque;
    float samples[128];
    for (unsigned i = 0; i < 64; ++i) {
        samples[i * 2] = 0.2f; samples[i * 2 + 1] = -0.2f;
    }
    AudioBufferList *in = buffers(1);
    in->mBuffers[0] = (AudioBuffer){2, sizeof(samples), samples};
    for (unsigned block = 0; block < 4000; ++block) {
        while (chring_available(shared->state->ring) > 2048) sched_yield();
        CHCaptureRender(0, NULL, in, NULL, NULL, NULL, shared->state);
    }
    atomic_store(&shared->done, true);
    free(in);
    return NULL;
}
static void uf_test_callback_concurrency(void) {
    CHRenderState state; uf_test_init_state(&state);
    atomic_store(&state.renderEnabled, true);
    CallbackThreads shared = {.state = &state};
    atomic_init(&shared.done, false);
    pthread_t producer;
    assert(pthread_create(&producer, NULL, uf_test_capture_thread, &shared) == 0);
    float left[64], right[64];
    AudioBufferList *out = buffers(2);
    out->mBuffers[0] = (AudioBuffer){1, sizeof(left), left};
    out->mBuffers[1] = (AudioBuffer){1, sizeof(right), right};
    unsigned rendered = 0;
    for (;;) {
        if (chring_available(state.ring) < 256) {
            if (atomic_load(&shared.done)) break;
            sched_yield(); continue;
        }
        CHOutputRender(&state, NULL, NULL, 0, 64, out);
        for (unsigned i = 0; i < 64; ++i) {
            assert(isfinite(left[i]) && fabsf(left[i]) <= 1);
            assert(fabsf(left[i] + right[i]) < 1e-7f);
        }
        ++rendered;
    }
    assert(pthread_join(producer, NULL) == 0);
    assert(rendered > 3500);
    assert(atomic_load(&state.captureCallbacks) == 4000);
    assert(atomic_load(&state.outputCallbacks) == rendered);
    assert(atomic_load(&state.fault) == 0);
    assert(chring_underruns(state.ring) == 0 && chring_overruns(state.ring) == 0);
    uf_test_destroy_state(&state); free(out);
    puts("PASS concurrent actual capture/output callbacks, counters and stereo data");
}
int main(void) {
    float capture[512], left[256], right[256];
    for (unsigned i=0;i<256;i++) { capture[i*2] = 0.2f; capture[i*2+1] = -0.2f; }
    AudioBufferList *in = buffers(1), *out = buffers(2);
    in->mBuffers[0] = (AudioBuffer){2,sizeof(capture),capture};
    out->mBuffers[0] = (AudioBuffer){1,sizeof(left),left};
    out->mBuffers[1] = (AudioBuffer){1,sizeof(right),right};
    CHStereoBuffer mapped = {0};
    assert(CHMapStereo(in,false,&mapped) && mapped.frames==256 && mapped.stride==2 && mapped.right==capture+1);
    assert(CHMapStereo(out,true,&mapped) && mapped.frames==256 && mapped.stride==1 && mapped.right==right);
    assert(!CHMapStereo(out,false,&mapped));
    out->mBuffers[1].mDataByteSize-=4; assert(!CHMapStereo(out,true,&mapped)); out->mBuffers[1].mDataByteSize+=4;
    puts("PASS exact callback stereo buffer mapping");
    CHRenderState state; uf_test_init_state(&state);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    assert(atomic_load(&state.signalBlocks)==2);
    assert(chring_available(state.ring)==512);
    CHOutputRender(&state,NULL,NULL,0,256,out);
    for (unsigned i=0;i<256;i++) assert(left[i]==0 && right[i]==0);
    assert(atomic_load(&state.outputReady)); assert(chring_available(state.ring)<512);
    puts("PASS pending capture drains without audible duplicate playback");
    atomic_store(&state.renderEnabled,true);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    CHOutputRender(&state,NULL,NULL,0,256,out);
    for (unsigned i=0;i<256;i++) assert(fabsf(left[i]-0.2f)<0.00001 && fabsf(right[i]+0.2f)<0.00001);
    assert(atomic_load(&state.fault)==0);
    puts("PASS active stereo processing preserves sign and channel order");
    atomic_store(&state.renderEnabled,false); in->mBuffers[0].mData=NULL;
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state); assert(atomic_load(&state.fault)==0);
    atomic_store(&state.renderEnabled,true);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state); assert(atomic_load(&state.fault)==1);
    assert(!atomic_load(&state.renderEnabled)); in->mBuffers[0].mData=capture;
    puts("PASS missing capture waits during startup and faults during processing");
    atomic_store(&state.fault,0); atomic_store(&state.renderEnabled,true);
    for (unsigned i=0;i<256;i++) left[i]=right[i]=0.5f;
    CHOutputRender(&state,NULL,NULL,0,257,out);
    assert(atomic_load(&state.fault)==2 && !atomic_load(&state.renderEnabled));
    for (unsigned i=0;i<256;i++) assert(left[i]==0 && right[i]==0);
    atomic_store(&state.alive,false);
    uint64_t before=atomic_load(&state.outputCallbacks);
    CHOutputRender(&state,NULL,NULL,0,256,out); assert(atomic_load(&state.outputCallbacks)==before);
    puts("PASS invalid or stopped output is zero and does not overrun storage");
    uf_test_destroy_state(&state);
    /* Check missing lists independently from a NULL channel data pointer. */
    uf_test_init_state(&state);
    CHCaptureRender(0,NULL,NULL,NULL,NULL,NULL,&state);
    assert(atomic_load(&state.fault)==0 && !atomic_load(&state.renderEnabled));
    atomic_store(&state.renderEnabled,true);
    CHCaptureRender(0,NULL,NULL,NULL,NULL,NULL,&state);
    assert(atomic_load(&state.fault)==1 && !atomic_load(&state.renderEnabled));
    assert(chring_available(state.ring)==0);
    atomic_store(&state.fault,0); atomic_store(&state.renderEnabled,true);
    in->mNumberBuffers=0;
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    assert(atomic_load(&state.fault)==1 && !atomic_load(&state.renderEnabled));
    puts("PASS absent capture lists immediately disable live rendering");
    uf_test_destroy_state(&state);
    uf_test_init_state(&state);
    in->mNumberBuffers=1;
    in->mBuffers[0].mNumberChannels=1;
    assert(!CHMapStereo(in,false,&mapped));
    in->mBuffers[0].mNumberChannels=2;
    in->mBuffers[0].mDataByteSize=3;
    assert(!CHMapStereo(in,false,&mapped));
    in->mBuffers[0].mDataByteSize=sizeof(capture);
    out->mBuffers[0].mData=NULL;
    assert(!CHMapStereo(out,true,&mapped));
    out->mBuffers[0].mData=left;
    assert(!CHMapStereo(NULL,true,&mapped));
    assert(!CHMapStereo(NULL,false,&mapped));
    puts("PASS malformed stereo channel counts, byte alignment, pointers and lists");
    atomic_store(&state.renderEnabled,true);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    CHOutputRender(&state,NULL,NULL,0,256,out);
    assert(atomic_load(&state.outputReady));
    CHOutputRender(&state,NULL,NULL,0,256,out);
    assert(!atomic_load(&state.outputReady));
    assert(chring_underruns(state.ring)==1);
    assert(atomic_load(&state.fault)==0);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    CHOutputRender(&state,NULL,NULL,0,256,out);
    assert(!atomic_load(&state.outputReady));
    for (unsigned i=0;i<256;i++) assert(left[i]==0 && right[i]==0);
    CHCaptureRender(0,NULL,in,NULL,NULL,NULL,&state);
    CHOutputRender(&state,NULL,NULL,0,256,out);
    assert(atomic_load(&state.outputReady));
    for (unsigned i=0;i<256;i++) assert(fabsf(left[i]-0.2f)<0.00001 && fabsf(right[i]+0.2f)<0.00001);
    puts("PASS output underrun readiness, silent rebuffering and capture recovery");
    uf_test_destroy_state(&state); free(in); free(out);
    uf_test_callback_concurrency();
    return 0;
}
