#import <Foundation/Foundation.h>
#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

/** A physical output that Core Audio currently exposes. */
@interface CHAudioDevice : NSObject
@property(nonatomic, copy, readonly) NSString *uid;
@property(nonatomic, copy, readonly) NSString *name;
@property(nonatomic, readonly) BOOL isDefaultOutput;
@property(nonatomic, readonly) BOOL supportsStereo;
@property(nonatomic, readonly) double sampleRate;
@end

/**
 Live local EQ for a stereo physical output on macOS 14.2 or later.
 Call lifecycle methods and read properties on the main thread.
 The engine never changes the Mac's default device or hardware volume.
 */
@interface CHAudioEngine : NSObject
@property(nonatomic, readonly, getter=isRunning) BOOL running;
@property(nonatomic, readonly, getter=isProcessing) BOOL processing;
@property(nonatomic, copy, readonly) NSString *statusText;
@property(nonatomic, readonly) double sampleRate;
@property(nonatomic, readonly) float peak;
@property(nonatomic, readonly) uint64_t clipCount;
@property(nonatomic, readonly) float headroomDB;
@property(nonatomic, readonly) double bufferDurationMS;
@property(nonatomic, strong, readonly, nullable) NSError *lastError;

/** Invoked on the main thread when a running session stops unexpectedly. */
@property(nonatomic, copy, nullable) void (^stateChanged)(NSError * _Nullable error);

+ (NSArray<CHAudioDevice *> *)outputDevices;
+ (nullable NSString *)defaultOutputDeviceUID;
/** Read-only device/route/volume metadata; does not create a tap or audio unit. */
+ (NSDictionary *)hardwareSnapshot;
/** Exercises invalid-route rejection and idempotent stop without requesting audio. */
+ (NSDictionary *)offStateSelfTest;

/**
 The selected device must already be the Mac's default output.
 This call may cause macOS to request System Audio Recording permission.
 No microphone or screen capture is requested.
 */
- (BOOL)startWithDeviceUID:(NSString *)uid error:(NSError * _Nullable * _Nullable)error;
- (void)stop;

/** Gains are in dB. These values persist across stopping and starting. */
- (void)setBass:(float)bass mid:(float)mid treble:(float)treble
           trim:(float)trim bypass:(BOOL)bypass;
@end

NS_ASSUME_NONNULL_END
