/* */
#ifndef UNTITLED1_FILTER_H
#define UNTITLED1_FILTER_H

#include <stddef.h>
#include <stdbool.h>

#include "buffer.h"
#define BEGIN do {
#define END   } while (0)

/***********************************************************************************************************************************
The basic filter which serves as a header for all other filter types.
***********************************************************************************************************************************/
typedef struct Filter {
    struct Filter *next;                                            // Points to the next filter in the pipeline
    struct FilterInterface *iface;                                  // The set of functions for processing requests.

    size_t  writeSize;         // We must accept writes of this size (before conversion)
    size_t  readSize;          // We must request reads of at least this size.

    // Passthrough objects - cache them so we don't have to scan ahead looking for a filter to process the event.
    struct Filter *nextOpen;
    struct Filter *nextRead;
    struct Filter *nextWrite;
    struct Filter *nextSync;
    struct Filter *nextClose;
    struct Filter *nextAbort;
    struct Filter *nextSize;  // TODO: Always use next?
} Filter;

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

typedef struct FilterInterface {
    FilterOpen fnOpen;
    FilterWrite fnWrite;
    FilterClose fnClose;
    FilterRead fnRead;
    FilterSync fnSync;
    FilterAbort fnAbort;
    FilterSize fnSize;
} FilterInterface;


Filter *filterInit(void *thisVoid, FilterInterface *iface, Filter *next);


#endif //UNTITLED1_FILTER_H
