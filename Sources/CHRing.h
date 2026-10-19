#ifndef CH_RING_H
#define CH_RING_H

#include <stddef.h>
#include <stdint.h>

/* Stereo single producer / single consumer buffer. The producer is the tap
   callback; the consumer is the output Audio Unit callback. No allocation or
   locking takes place in write/read/reset. Both endpoints must have the same
   nominal sample rate. Interpolation corrects only small clock drift. */
typedef struct CHRing CHRing;

CHRing *chring_create(size_t capacity_frames, size_t target_backlog_frames);
void chring_destroy(CHRing *ring);

size_t chring_write_stereo(CHRing *ring,
    const float *left, size_t left_stride,
    const float *right, size_t right_stride, size_t frames);

/* Always fills both output channels. Returns the frames supplied from the
   buffer, with any remaining frames filled with silence. */
size_t chring_read_stereo(CHRing *ring,
    float *left, size_t left_stride,
    float *right, size_t right_stride, size_t frames);

/* Only the consumer callback may reset. Capture may continue concurrently. */
void chring_reset_consumer(CHRing *ring);
size_t chring_available(const CHRing *ring);
uint64_t chring_underruns(const CHRing *ring); /* events after initial priming */
uint64_t chring_overruns(const CHRing *ring);  /* dropped input frames */

#endif
