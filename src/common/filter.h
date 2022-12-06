/**
 * A Filter processes events, either handling them itself or by passing
 * them "down the Pipeline" to subsequent filters.
 * A sequence of Filters forms a Pipeline, where the first filter is called
 * a Source and the final filter is called a Sink.
 *
 * Events are geared toward typical file management operations like "Read",
 * "Write", "Seek" and "Open", but new events
 * can be easily defined. If a filter doesn't recognize an event,
 * the event passes down the pipeline until some other filter can process it.
 * (Note that events are implemented as simple procedure calls.)
 *
 * Data flows between filters in fixed size "blocks", where a block is a
 * chunk of data which fits in memory. A filter transforms data blocks,
 * changing either the content or the size of the block. Since sizes can change,
 * block size information is
 * communicated throughout the pipeline with the "BlockSize" event,
 * allowing each filter to state its size requirements and to know its neighbor's block size.
 *
 * Block sizes between stages do not always match. It is always acceptable for a predecessor's block
 * size to be a multiple of a successor's block size. If block sizes are otherwise incompatible, it
 * is possible to insert a "buffer" filter into the stream which buffers data into the appropriate
 * block size.
 *
 * The resulting output file consists of a sequence of equally sized blocks, possibly followed by
 * a final, partial block. Some filters (including compression) may produce variable sized blocks;
 * those filters need to maintain the appearance of fixed size blocks, even though the resulting
 * output is not actually fixed size.
 */

#ifndef UNTITLED1_FILTER_H
#define UNTITLED1_FILTER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/error.h"

#define BEGIN do {
#define END   } while (0)

/* We support 64 bit seeks, either to blocks or bytes. */
typedef uint64_t pos_t;
#define FILE_END_POSITION ((pos_t)-1)

/* This structure is an abstract header which is the first element of all filter types. */
typedef struct Filter {
    struct Filter *next;            /* Points to the next filter in the pipeline */
    struct FilterInterface *iface;  /* The set of functions for processing requests. */

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
    struct Filter *nextBlockSize;
    struct Filter *nextSeek;
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
typedef pos_t (*FilterSeek)(void *this, pos_t position, Error *error);
typedef size_t (*FilterBlockSize)(void *this, size_t size, Error *error);

typedef struct FilterInterface {
    FilterOpen fnOpen;
    FilterWrite fnWrite;
    FilterClose fnClose;
    FilterRead fnRead;
    FilterSync fnSync;
    FilterAbort fnAbort;
    FilterBlockSize fnBlockSize;
    FilterSeek fnSeek;
} FilterInterface;

/* Initialize the generic parts of a filter */
Filter *filterInit(void *thisVoid, FilterInterface *iface, Filter *next);

/* Some possibly helpful stubs. */
void badSeek(Filter *this, size_t position, Error *error);

inline static size_t sizeMin(size_t a, size_t b) {return (a<b)?a:b;}
inline static size_t sizeMax(size_t a, size_t b) {return (a>b)?a:b;}
inline static size_t sizeRoundUp(size_t size, size_t factor) {return (size + factor - 1) / factor * factor;}

#endif /*UNTITLED1_FILTER_H */
