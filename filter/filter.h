/* */
#ifndef UNTITLED1_FILTER_H
#define UNTITLED1_FILTER_H

#include <stddef.h>
#include <stdbool.h>
#include "request.h"

/***********************************************************************************************************************************
The basic filter which serves as a header for all other filter types.
***********************************************************************************************************************************/
typedef struct Filter {
    struct Filter *next;                                            // Points to the next filter in the pipeline
    struct FilterInterface *iface;                                  // The set of functions for processing requests.
} Filter;

/***********************************************************************************************************************************
A set of functions a filter provides for dealing with each type of request.
***********************************************************************************************************************************/
typedef void (*FilterService)(void *this, void *request);

typedef struct FilterInterface {
    FilterService fnOpen;
    FilterService fnWrite;
    FilterService fnClose;

    FilterService fnRead;
    FilterService fnPeek;

    FilterService fnSeek;
    FilterService fnSync;
    FilterService fnAbort;
} FilterInterface;

#endif //UNTITLED1_FILTER_H
