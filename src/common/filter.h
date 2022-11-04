/**
 * A Filter processes events, either handling them itself or by passing
 * them "down the Pipeline" to subsequent filters.
 * A sequence of Filters forms a Pipeline, where the first filter is called
 * a Source and the final filter is called a Sink.
 *
 * Events are geared toward typical file management operations, but new events
 * can be easily defined. If a filter doesn't recognize an event,
 * the event passes down the pipeline until some filter can process it.
 *
 * Two of the events, Read and Write, implement the flow of data.
 * As data flows from filter to filter, it may be buffered or transformed.
 * Frequently, the amount of data flowing into a filter and the amount of
 * data flowing fram a filter will be different. Calculating appropriate
 * buffer sizes between multiple filters can be tricky, and a special
 * Size() event helps coordinate this sizing process.
 *
 * Reads and Writes do not normally transfer their entire buffers in a single call.
 *   - Writes can try to write larger buffers, but they will typically
 *     be truncated on a block boundary. However, WriteAll will write
 *     the entire buffer, including partial blocks.
 *   - Reads must always request more than this->readSize bytes or there
 *     may not be enough space to transform the data being read.
 *     ReadAll will attempt to read multiple blocks, but it will usually
 *     stop early on a block boundary.
 *
 * For both reads and writes, we can allocate larger buffers and transform
 * multiple basic blocks in one call.
***********************************************************************************************************************************/

#ifndef UNTITLED1_FILTER_H
#define UNTITLED1_FILTER_H

#include <stddef.h>
#include <stdbool.h>

#include "buffer.h"
#define BEGIN do {
#define END   } while (0)

/* This structure is an abstract header which is the first element of all filter types. */
typedef struct Filter {
    struct Filter *next;            /* Points to the next filter in the pipeline */
    struct FilterInterface *iface;  /* The set of functions for processing requests. */

    /* When our predecessor writes to us, we must be prepared to accept this many bytes */
    size_t  writeSize;

    /* When we read from our successor, we must issue a read with at least this many bytes */
    size_t  readSize;

    /*
     * Passthrough objects, one for each type of event.
     * We save a pointer to the next object which processes that event,
     * so we don't have to scan ahead looking for them.
     */
    struct Filter *nextOpen;
    struct Filter *nextRead;
    struct Filter *nextWrite;
    struct Filter *nextSync;
    struct Filter *nextClose;
    struct Filter *nextAbort;
    struct Filter *nextSize;
} Filter;

/***********************************************************************************************************************************
A set of functions a filter provides, one for each type of event.
***********************************************************************************************************************************/
typedef Error (*FilterOpen)(void *this, char *path, int mode, int perm);
typedef size_t (*FilterRead)(void *this, Byte *buf, size_t size, Error *error);
typedef size_t (*FilterWrite)(void *this, Byte *buf, size_t size, Error *error);
typedef void (*FilterClose)(void *this, Error *error);
typedef void (*FilterSync)(void *this, Error *error);
typedef void (*FilterAbort)(void *this, Error *error);
typedef size_t (*FilterSize)(void *this, size_t size);

typedef struct FilterInterface {
    FilterOpen fnOpen;
    FilterWrite fnWrite;
    FilterClose fnClose;
    FilterRead fnRead;
    FilterSync fnSync;
    FilterAbort fnAbort;
    FilterSize fnSize;
} FilterInterface;

/* Initialize the generic parts of a filter */
Filter *filterInit(void *thisVoid, FilterInterface *iface, Filter *next);

#endif /*UNTITLED1_FILTER_H */
