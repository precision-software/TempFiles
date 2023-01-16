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
#include "iostack.h"

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
#define passThroughDelete(this, path, error) passThrough(Delete, this, path, error)


/* Helper function to ensure all the data is written. */
size_t passThroughWriteAll(void *this, const Byte *buf, size_t size, Error *error);
size_t passThroughReadAll(void *this, Byte *buf, size_t size, Error *error);
size_t passThroughReadSized(void *this, Byte *header, size_t size, Error *error);
size_t passThroughWriteSized(void *this, Byte *header, size_t size, Error *error);

/* Helper functions to read/write integers in network byte order (big endian) */
#define passThroughPut1(this, value, error)  filePut1(this, value, error)
#define passThroughPut2(this, value, error)  filePut2(this, value, error)
#define passThroughPut4(this, value, error)  filePut4(this, value, error)
#define passThroughPut8(this, value, error)  filePut8(this, value, error)
#define passThroughGet1(this, error)         fileGet1(this, error)
#define passThroughGet2(this, error)         fileGet2(this, error)
#define passThroughGet4(this, error)         fileGet4(this, error)
#define passthroughGet8(this, error)         fileGet8(this, errorO)

#endif /* COMMON_PASSTHROUGH_H */
