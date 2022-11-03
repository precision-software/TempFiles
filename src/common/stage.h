/* */
#ifndef UNTITLED1_ Stage_H
#define UNTITLED1_ Stage_H

#include <stddef.h>
#include <stdbool.h>

#include "buffer.h"
#define BEGIN do {
#define END   } while (0)

/***********************************************************************************************************************************
The basic Stage which serves as an abstract header for all other  Stage types.
Note a  Stage processes events, either handling them itself or by passing them "down the line" to
subsequent filters.

Two of the events, Read and Write, implement the flow of data. As data flows from filter to filter, it may be buffered
or transformed. Frequently, the amount of data flowing into a filter and the amount of data flowing fram a filter will
be different. The expected size of data at each stage determines the size of the filter's internal buffers.

This sizing process is implemented during initialization, where a "Size" event tells a filter how much unprocessed
data can be read or written in a single call. In turn, the filter tells its neighbors how much transformed data they need to read or
write. Each filter must respect the bounds set by its neighbors.

This approach to sizing is driven by the openSSL library, which simply fails if the destination buffer is too small. By agreeing
on buffer sizes during configuration, there should be never be a situation where a buffer is too small to accept results
from openSSl.

When processing data, a filter is passed the address of its predecessor's buffer, and to knows the details of its internal buffer.
A filter cannot directly access a successor's buffer. This decision influences how data is processed and buffered.
 - On Writes, a filter transforms data from its predecessor's buffer (as passed by Write) into its internal buffer.
   The filter may have to spill data to make room in its internal buffer.
 - On Reads, a filter transforms data from its internal buffer into its predecessor's buffer (as passed by Read).
   The filter may have to fill the buffer (read from successor).

 Managing buffer sizes and I/O sizes is a bit tricky. We know the upperbound of how much data is produced for a given input size,
 but we don't rely on much more than that. The idea is to pick a block size for unprocessed data, and to calculate the corresponding
 blocksize for the processed data. This calculation is propagated up and down the pipeline, allowing each filter to allocate a
 suitably sized buffer.

 The following values are setup when the "pipeline" is constructed.
 - this->writeSize  The blocksize of transformed bytes after we have transformed them into our buffer.
 - next->readSize   The basic blocksize of bytes we are requesting from our successor.
 - this->readSize   The blocksize of transformed bytes after we transfer them into our callers buffer.

Reads and Writes do not normally transfer their entire buffers in a single call.
 - Writes can try to write larger buffers, but they will typically be truncated on a block boundary.
   However, WriteAll will write the entire buffer, including partial blocks.
 - Reads must always request more than this->readSize bytes or there may not be enough space for the successor to transform data.
   ReadAll will attempt to read multiple blocks, but it will usually stop a bit early on a block boundary.
   Subsequent Reads will continue reading blocks up until EOF.

For both reads and writes, we can allocate a larger buffer and transform multiple basic blocks in one call.
***********************************************************************************************************************************/
typedef struct Stage {
    struct Stage *next;                                            /* Points to the next filter in the pipeline */
    struct FilterInterface *iface;                                  /* The set of functions for processing requests. */

    size_t  writeSize;         /* max unprocessed bytes from predecessor we guarantee we can process */
    size_t  readSize;          /* max processed bytes we know we can transform to predecessor with overflowing their buffer. */

    /* Passthrough objects - cache them, so we don't have to scan ahead looking for the next filter to process the event. */
    struct Stage *nextOpen;
    struct Stage *nextRead;
    struct Stage *nextWrite;
    struct Stage *nextSync;
    struct Stage *nextClose;
    struct Stage *nextAbort;
    struct Stage *nextSize;
} Stage;

/***********************************************************************************************************************************
A set of functions a filter provides for dealing with each type of request.
***********************************************************************************************************************************/
typedef void (*FilterService)(void *this, void *request);

typedef Error (*FilterOpen)(void *this, char *path, int mode, int perm);
typedef size_t (*FilterRead)(void *this, Byte *buf, size_t size, Error *error);
typedef size_t (*FilterWrite)(void *this, Byte *buf, size_t size, Error *error);
typedef void (*FilterClose)(void *this, Error *error);
typedef void (*FilterSync)(void *this, Error *error);
typedef void (*FilterAbort)(void *this, Error *error);
typedef size_t (*FilterSize)(void *this, size_t size);

typedef struct StageInterface {
    FilterOpen fnOpen;
    FilterWrite fnWrite;
    FilterClose fnClose;
    FilterRead fnRead;
    FilterSync fnSync;
    FilterAbort fnAbort;
    FilterSize fnSize;
} StageInterface;


Stage *filterInit(void *thisVoid, FilterInterface *iface, Stage *next);

Buffer *filterAllocateBuffer(Stage *this, size_t size);


#endif /*UNTITLED1_FILTER_H */
