/* Extracted engine teardown, with Foundation and fake HAL handles only.
   No Core Audio framework is linked and no device or capture API runs. */
#import <Foundation/Foundation.h>
#include "CHEQ.h"
#include "CHRing.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef int OSStatus;
typedef unsigned AudioObjectID;
typedef unsigned AudioDeviceID;
typedef void *AudioDeviceIOProcID;
typedef void *AudioUnit;
typedef int CATapMuteBehavior;
enum { kAudioObjectUnknown = 0, kAudioHardwareBadObjectError = -99,
       CATapUnmuted = 0, CHErrorCleanup = 6 };
enum { STOP_CAPTURE, DETACH_CAPTURE, STOP_OUTPUT, UNINITIALIZE_OUTPUT,
       DISPOSE_OUTPUT, UNMUTE, DESTROY_AGGREGATE, DESTROY_TAP, OP_COUNT };
static unsigned failures, calls[OP_COUNT];
static bool badObjects;
static OSStatus operation(unsigned op) {
    ++calls[op];
    if (!(failures & (1u << op))) return noErr;
    if (badObjects && (op == DESTROY_AGGREGATE || op == DESTROY_TAP))
        return kAudioHardwareBadObjectError;
    return -(OSStatus)(100 + op);
}
static OSStatus AudioDeviceStop(AudioDeviceID a, AudioDeviceIOProcID b) {
    assert(a && b); return operation(STOP_CAPTURE);
}
static OSStatus AudioDeviceDestroyIOProcID(AudioDeviceID a, AudioDeviceIOProcID b) {
    assert(a && b); return operation(DETACH_CAPTURE);
}
static OSStatus AudioOutputUnitStop(AudioUnit unit) {
    assert(unit); return operation(STOP_OUTPUT);
}
static OSStatus AudioUnitUninitialize(AudioUnit unit) {
    assert(unit); return operation(UNINITIALIZE_OUTPUT);
}
static OSStatus AudioComponentInstanceDispose(AudioUnit unit) {
    assert(unit); return operation(DISPOSE_OUTPUT);
}
static OSStatus AudioHardwareDestroyAggregateDevice(AudioDeviceID device) {
    assert(device); return operation(DESTROY_AGGREGATE);
}
static OSStatus AudioHardwareDestroyProcessTap(AudioObjectID tap) {
    assert(tap); return operation(DESTROY_TAP);
}
static NSError *CHError(int code, NSString *message, OSStatus status) {
    return [NSError errorWithDomain:@"test.engine" code:code
                          userInfo:@{NSLocalizedDescriptionKey: message,
                                     @"status": @(status)}];
}
typedef struct {
    CHEQ *eq;
    CHRing *ring;
    atomic_bool alive;
    atomic_bool renderEnabled;
} CHRenderState;

@interface CHTeardownFixture : NSObject {
@public
    NSTimer *_watchdog;
    CHRenderState *_render;
    AudioDeviceID _deviceID, _aggregateID;
    AudioObjectID _tapID;
    AudioDeviceIOProcID _ioProcID;
    AudioUnit _outputUnit;
    BOOL _outputStarted, _outputInitialized, _cleanupFailed;
    NSObject *_tapDescription;
    NSString *_deviceUID;
}
@property(nonatomic) BOOL running, processing;
@property(nonatomic) double sampleRate;
@property(nonatomic, strong) NSError *lastError;
@property(nonatomic, copy) NSString *statusText;
@property(nonatomic, copy) void (^stateChanged)(NSError *);
- (OSStatus)applyMuteBehavior:(CATapMuteBehavior)behavior;
- (void)finishWithError:(NSError *)failure notify:(BOOL)notify;
@end

@implementation CHTeardownFixture
- (OSStatus)applyMuteBehavior:(CATapMuteBehavior)behavior {
    assert(behavior == CATapUnmuted);
    if (!_tapID || !_tapDescription) return noErr;
    return operation(UNMUTE);
}
/* CHAUDIO_TEARDOWN */
@end

static CHTeardownFixture *session(void) {
    CHTeardownFixture *s = [CHTeardownFixture new];
    s->_render = calloc(1, sizeof(*s->_render));
    assert(s->_render);
    s->_render->eq = cheq_create(48000);
    s->_render->ring = chring_create(256, 16);
    assert(s->_render->eq && s->_render->ring);
    atomic_init(&s->_render->alive, true);
    atomic_init(&s->_render->renderEnabled, true);
    s->_deviceID = 1; s->_aggregateID = 2; s->_tapID = 3;
    s->_ioProcID = (void *)4; s->_outputUnit = (void *)5;
    s->_outputStarted = s->_outputInitialized = YES;
    s->_tapDescription = [NSObject new]; s->_deviceUID = @"fake-device";
    s.running = s.processing = YES; s.sampleRate = 48000;
    return s;
}

int main(void) {
    @autoreleasepool {
        for (unsigned mask = 0; mask < (1u << OP_COUNT); ++mask) {
            failures = mask; badObjects = false;
            memset(calls, 0, sizeof(calls));
            CHTeardownFixture *s = session();
            __block unsigned notifications = 0;
            s.stateChanged = ^(NSError *error) {
                (void)error; ++notifications;
            };
            [s finishWithError:nil notify:YES];
            assert(!s.running && !s.processing && s.sampleRate == 0);
            assert(notifications == 1);
            for (unsigned op = 0; op < OP_COUNT; ++op) assert(calls[op] == 1);
            bool captureDetached = !(mask & (1u << DETACH_CAPTURE)) ||
                                   !(mask & (1u << DESTROY_AGGREGATE));
            bool outputDetached = !(mask & (1u << DISPOSE_OUTPUT));
            assert((s->_render == NULL) == (captureDetached && outputDetached));
            if (s->_render) {
                assert(!atomic_load(&s->_render->alive));
                assert(!atomic_load(&s->_render->renderEnabled));
            }
            bool incomplete = (mask & (1u << DESTROY_AGGREGATE)) ||
                              (mask & (1u << DESTROY_TAP)) || !outputDetached;
            assert(s->_cleanupFailed == incomplete);
            assert((s.lastError != nil) == incomplete);
            if (incomplete) assert(s.lastError.code == CHErrorCleanup);
            failures = 0;
            [s finishWithError:nil notify:NO];
            assert(!s->_cleanupFailed && !s->_render && !s->_aggregateID &&
                   !s->_tapID && !s->_ioProcID && !s->_outputUnit);
            assert(s.lastError == nil && notifications == 1);
            [s finishWithError:nil notify:NO];
            assert(notifications == 1 && !s->_render);
        }
        puts("PASS all 256 teardown failure combinations, retained callback contexts, retries and idempotence");
        failures = (1u << DESTROY_AGGREGATE) | (1u << DESTROY_TAP);
        badObjects = true;
        CHTeardownFixture *s = session();
        NSError *original = CHError(3, @"original startup failure", -11);
        [s finishWithError:original notify:NO];
        assert(!s->_cleanupFailed && !s->_render && s.lastError == original);
        puts("PASS already-destroyed objects accepted and original startup error retained");
        failures = 0; badObjects = false;
        s = [CHTeardownFixture new];
        [s finishWithError:nil notify:NO];
        [s finishWithError:original notify:NO];
        assert(!s->_cleanupFailed && !s->_render && s.lastError == original);
        puts("PASS partial startup with no resources and repeated stop");
        /* Model ownership at the startup allocation checkpoints. Only
           synthetic handles exist; the extracted cleanup body is unchanged. */
        for (unsigned stage = 0; stage < 10; ++stage) {
            s = session();
            if (stage < 9) s->_ioProcID = NULL;
            if (stage < 8) s->_aggregateID = 0;
            if (stage < 7) { s->_tapID = 0; s->_tapDescription = nil; }
            if (stage < 6) s->_outputStarted = NO;
            if (stage < 5) s->_outputInitialized = NO;
            if (stage < 4) s->_outputUnit = NULL;
            if (stage < 3) {
                chring_destroy(s->_render->ring); s->_render->ring = NULL;
            }
            if (stage < 2) {
                cheq_destroy(s->_render->eq); s->_render->eq = NULL;
            }
            if (stage < 1) { free(s->_render); s->_render = NULL; }
            [s finishWithError:original notify:NO];
            assert(!s->_cleanupFailed && !s->_render && !s->_outputUnit &&
                   !s->_tapID && !s->_aggregateID && !s->_ioProcID);
            assert(s.lastError == original);
        }
        puts("PASS cleanup at ten partial-start allocation checkpoints");
    }
    return 0;
}
