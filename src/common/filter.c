/*
 *
 */
#include <assert.h>
#include "common/filter.h"
#include "common/passThrough.h"

/* Helper macro to find the next filter which processes a given event. */
#define getNext(Event, this) \
    ((this->next->iface->fn##Event == NULL) \
        ? this->next->next##Event \
        : this->next)


Filter *filterInit(void *thisVoid, FilterInterface *iface, Filter *next)
{
    /* Link us up with our successor. */
    Filter *this = thisVoid;
    *this = (Filter){.next = next, .iface = iface};

    /* For each event, point to the next filter which processes that event. */
    this->nextOpen = getNext(Open, this);
    this->nextRead = getNext(Read, this);
    this->nextWrite = getNext(Write, this);
    this->nextClose = getNext(Close, this);
    this->nextSync = getNext(Sync, this);
    this->nextAbort = getNext(Abort, this);
    this->nextBlockSize = getNext(BlockSize, this);
    this->nextSeek = getNext(Seek, this);

    /* Each filter must provide a "Size" routine in its interface. */
    assert(this->iface->fnBlockSize != NULL);

    return this;
}

void badSeek(Filter *this, size_t position, Error *error)
{
    filterError(error, "Filter does not implement Seek().");
}
