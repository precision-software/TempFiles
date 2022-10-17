//
// Created by John Morris on 9/24/22.
//

#include "filter.h"

/****
Macro which finds the "next" filter which will process the request.
Avoids a lot of repetition, intended for lazy typists.

*/

#define setNextFilter(this, Call) \
    do {   \
        Filter *_next;               \
        for (_next=((Filter *)this)->next; _next != NULL; _next=_next->next)    \
            if (_next->iface->fn##Call != NULL) break;    \
        if (_next == NULL)                                           \
            _next = &invalidFilter;                       \
        ((Filter *)this)->next##Call = _next;               \
    } while (0)


static Error invalidMethod = (Error){.code=errorCodeFilter, .msg="Filter method not implemented ."};

static Error invalidOpen(void *this, char *path, int mode, int perm) {return invalidMethod;}
static size_t invalidRead(void *this, Byte *buf, size_t size, Error *error) {*error = invalidMethod; return 0;}
static size_t invalidWrite(void *this, Byte *buf, size_t size, Error *error) {*error = invalidMethod; return 0;}
static void invalidClose(void *this, Error *error) {*error = invalidMethod;}

static FilterInterface invalidFilterInterface = (FilterInterface)
{
    .fnOpen = invalidOpen,
    .fnRead = invalidRead,
    .fnWrite = invalidWrite,
    .fnClose = invalidClose
};

Filter invalidFilter = (Filter){.iface = &invalidFilterInterface};


void setNext(void *this)
{
    setNextFilter(this, Open);
    setNextFilter(this, Write);
    setNextFilter(this, Read);
    setNextFilter(this, Close);
}
