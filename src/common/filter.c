//
// Created by John Morris on 9/24/22.
//

#include "filter.h"


#define getNext(Event, this) \
    ((this->next->iface->fn##Event == NULL) \
        ? this->next->next##Event \
        : this->next)


Filter *filterInit(void *thisVoid, FilterInterface *iface, Filter *next)
{
    Filter *this = thisVoid;
    *this = (Filter){.next = next, .iface = iface};

    this->nextOpen = getNext(Open, this);
    this->nextRead = getNext(Read, this);
    this->nextWrite = getNext(Write, this);
    this->nextClose = getNext(Close, this);
    this->nextSync = getNext(Sync, this);
    this->nextAbort = getNext(Abort, this);
    this->nextSize = getNext(Size, this);

    return this;
}
