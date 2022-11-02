//
// Created by John Morris on 9/24/22.
//

#include "common/filter.h"
#include "common/passThrough.h"


#define getNext(Event, this) \
    ((this->next->iface->fn##Event == NULL) \
        ? this->next->next##Event \
        : this->next)


Filter *filterInit(void *thisVoid, FilterInterface *iface, Filter *next)
{
    // Link us up with our successor, and link our sucessor with us.
    Filter *this = thisVoid;
    *this = (Filter){.next = next, .iface = iface};
    this->next->prev = this;

    this->nextOpen = getNext(Open, this);
    this->nextRead = getNext(Read, this);
    this->nextWrite = getNext(Write, this);
    this->nextClose = getNext(Close, this);
    this->nextSync = getNext(Sync, this);
    this->nextAbort = getNext(Abort, this);
    this->nextSize = getNext(Size, this);

    // Each filter must provide a "Size" routine in its interface.
    assert(this->iface->fnSize != NULL);

    return this;
}
