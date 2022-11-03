/* */
/* Created by John Morris on 10/10/22. */
/* */

#ifndef UNTITLED1_PASSTHROUGH_H
#define UNTITLED1_PASSTHROUGH_H
#include "filter.h"

extern FilterInterface passThroughInterface;
#define passThrough(Event, this, ...)   ((Filter*)this)->next##Event->iface->fn##Event(((Filter*)this)->next##Event, __VA_ARGS__)
#define passThroughOpen(this, path, mode, perm) passThrough(Open, this, path, mode, perm)
#define passThroughRead(this, buf, size, error) passThrough(Read, this, buf, size, error)
#define passThroughWrite(this, buf, size, error) passThrough(Write, this, buf, size, error)
#define passThroughClose(this, error) passThrough(Close, this, error)
#define passThroughAbort(this, error)  passThrough(Abort, this, error)
#define passThroughSync(this, error) passThrough(Sync, this, error)
#define passThroughSize(this, size) passThrough(Size, this, size);

/* Helper function to ensure all the data is written. */
size_t passThroughWriteAll(void *this, Byte *buf, size_t size, Error *error);
size_t passThroughReadAll(void *this, Byte *buf, size_t size, Error *error);

#endif /*UNTITLED1_PASSTHROUGH_H */
