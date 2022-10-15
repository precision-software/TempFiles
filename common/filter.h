/* */
#ifndef UNTITLED1_FILTER_H
#define UNTITLED1_FILTER_H

#include <stddef.h>
#include <stdbool.h>
#include "request.h"
#include "common/buffer.h"

/***********************************************************************************************************************************
The basic filter which serves as a header for all other filter types.
***********************************************************************************************************************************/
typedef struct Filter {
    struct Filter *next;                                            // Points to the next filter in the pipeline
    struct FilterInterface *iface;                                  // The set of functions for processing requests.
    size_t blockSize;                                               // The block size we expect, 1 if buffered.
    Buffer *buf;                                                    // Our internal buffer, if we want to share it.
} Filter;

/***********************************************************************************************************************************
A set of functions a filter provides for dealing with each type of request.
***********************************************************************************************************************************/
typedef void (*FilterService)(void *this, void *request);

typedef Error (*FilterOpen)(void *this, char *path, int mode, int perm);
typedef size_t (*FilterRead)(void *this, Byte *buf, size_t size, Error *error);
typedef size_t (*FilterWrite)(void *this, Byte *buf, size_t size, Error *error);
typedef void (*FilterClose)(void *this, Error *error);

typedef struct FilterInterface {
    FilterOpen fnOpen;
    FilterWrite fnWrite;
    FilterClose fnClose;
    FilterRead fnRead;
} FilterInterface;

#endif //UNTITLED1_FILTER_H
