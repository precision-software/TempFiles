/**
 * A collection of functions for sending an event to the next filter in the pipeline.
 * To avoid lots of repetition, these "event calls" are coded as macros.
 * The word "passThrough" isn't competely accurate, but it does capture
 * the idea of sending the event on to the next filter which can handle it.
 *
 * In practice, we cache the next downstream filter which is able to process
 * each event, so we can dispatch directly and don't have to scan the pipeline
 * looking for an appropriate filter.  (See filter.h)
 */
#ifndef COMMON_PASSTHROUGH_H
#define COMMON_PASSTHROUGH_H
#include "common/filter.h"

extern FilterInterface passThroughInterface;
#define passThrough(Event, this, ...)   ((Filter*)this)->next##Event->iface->fn##Event(((Filter*)this)->next##Event, __VA_ARGS__)
#define passThroughOpen(this, path, oflags, mode, error) passThrough(Open, this, path, oflags, mode, error)
#define passThroughRead(this, buf, size, error) passThrough(Read, this, buf, size, error)
#define passThroughWrite(this, buf, size, error) passThrough(Write, this, buf, size, error)
#define passThroughClose(this, error) passThrough(Close, this, error)
#define passThroughAbort(this, error)  passThrough(Abort, this, error)
#define passThroughSync(this, error) passThrough(Sync, this, error)
#define passThroughSeek(this, position, error) passThrough(Seek, this, position, error)
#define passThroughBlockSize(this, size, error) passThrough(BlockSize, this, size, error)


/* Helper function to ensure all the data is written. */
size_t passThroughWriteAll(void *this, Byte *buf, size_t size, Error *error);
size_t passThroughReadAll(void *this, Byte *buf, size_t size, Error *error);
size_t passThroughReadSized(void *this, Byte *header, size_t size, Error *error);
size_t passThroughWriteSized(void *this, Byte *header, size_t size, Error *error);

bool passThroughPut1(void *this, size_t value, Error *error);
bool passThroughPut2(void *this, size_t value, Error *error);
bool passThroughPut4(void *this, size_t value, Error *error);
bool passThroughPut8(void *this, size_t value, Error *error);

size_t passThroughGet1(void *this, Error *error);
size_t passThroughGet2(void *this, Error *error);
size_t passThroughGet4(void *this, Error *error);
size_t passThroughGet8(void *this, Error *error);


#endif /* COMMON_PASSTHROUGH_H */
