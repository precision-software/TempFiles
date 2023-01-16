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
 * chunk of data which fits in memory. A filter transforms blocks,
 * changing either the content or the size of the blocks. Since sizes can change,
 * block size information is negotiated
 * throughout the pipeline with the "BlockSize" event,
 * allowing each filter to state its size requirements and to know its neighbor's block size.
 *
 * Block sizes between stages do not always match. It is always acceptable for a predecessor's block
 * size to be a multiple of a successor's successors size. If block sizes are otherwise incompatible, it
 * is possible to insert a "buffered" filter into the stream which buffers data into the appropriate
 * block size.
 *
 * The resulting output file consists of a sequence of equally sized blocks, possibly followed by
 * a final, partial block. Some filters (including compression) may produce variable sized blocks;
 * those filters need to maintain the appearance of fixed size blocks, even though the resulting
 * output is not actually fixed size.
 *
 * The "Seek" event allows positioning to a random block. If seeking to FILE_END_POSITION,
 * the event will return the file size and position the file to:   (TODO: Verify ... things have changed)
 *    1) The beginning of the final partial block, or
 *    2) if no partial blocks, the actial EOF,
 *    3) In some cases, the beginning of the final block, even if full.
 *
 * Condition 3) occurs because, without examining the last block, it may not
 * be possible to determine if it partial or full.  (For example, encryption with padding.)
 * (Consider resolving condition 3) in the filter itself, so 1) and 2) always apply.)
 *
 * When the record size is 1 byte, a seek to FILE_END_POSITION will always point to EOF
 * and return the number of bytes stored in the file.
 */

#ifndef COMMON_FILTER_H
#define COMMON_FILTER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "iostack_error.h"

#define BEGIN do {
#define END   } while (0)

/* We support 64 bit seeks, either to blocks or bytes. */
#define FILE_END_POSITION ((off_t)-1)

#define MAX_BLOCK_SIZE (16*1024*1024)

/* This structure is an abstract header which is the first element of all filter types. */
typedef struct Filter {
    struct Filter *next;            /* Points to the next filter in the pipeline */
    struct FilterInterface *iface;  /* The set of functions for processing requests. */

    /*
     * Passthrough objects, one for each type of event.
     * We cache a pointer to the next object which processes that event,
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
    struct Filter *nextDelete;
} Filter;

/***********************************************************************************************************************************
A set of functions a filter provides, one for each type of event.
***********************************************************************************************************************************/
typedef Filter *(*FilterOpen)(void *this, const char *path, int mode, int perm, Error *error);
typedef size_t (*FilterRead)(void *this, Byte *buf, size_t size, Error *error);
typedef size_t (*FilterWrite)(void *this, const Byte *buf, size_t size, Error *error);
typedef void (*FilterClose)(void *this, Error *error);
typedef void (*FilterSync)(void *this, Error *error);
typedef void (*FilterAbort)(void *this, Error *error);
typedef size_t (*FilterSize)(void *this, size_t size);
typedef off_t (*FilterSeek)(void *this, off_t position, Error *error);
typedef size_t (*FilterBlockSize)(void *this, size_t size, Error *error);
typedef size_t (*FilterDelete)(void *this, char *path, Error *error);

typedef struct FilterInterface {
    FilterOpen fnOpen;
    FilterWrite fnWrite;
    FilterClose fnClose;
    FilterRead fnRead;
    FilterSync fnSync;
    FilterAbort fnAbort;
    FilterBlockSize fnBlockSize;
    FilterSeek fnSeek;
    FilterDelete fnDelete;
} FilterInterface;

/* Initialize the generic parts of a filter */
void *filterInit(void *thisVoid, FilterInterface *iface, void *next);

/* Some possibly helpful stubs. */
void badSeek(Filter *this, size_t position, Error *error);

inline static size_t sizeMin(size_t a, size_t b) {return (a<b)?a:b;}
inline static size_t sizeMax(size_t a, size_t b) {return (a>b)?a:b;}
inline static size_t sizeRoundDown(size_t size, size_t factor) {return size - size % factor;}
inline static size_t sizeRoundUp(size_t size, size_t factor) {return sizeRoundDown(size + factor - 1, factor);}

#ifndef MAXPGPATH
#define MAXPGPATH (1024)
#endif


#endif /* COMMON_FILTER_H */
