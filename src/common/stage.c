/* */
/* Created by John Morris on 9/24/22. */
/* */

#include "common/stage.h"
#include "common/passThrough.h"


#define getNext(Event, this) \
    ((this->next->iface->fn##Event == NULL) \
        ? this->next->next##Event \
        : this->next)


Stage * StageInit(void *thisVoid,  StageInterface *iface, Stage *next)
{
    /* Link us up with our successor, and link our sucessor with us. */
    Stage *this = thisVoid;
    *this = (Stage){.next = next, .iface = iface};

    this->nextOpen = getNext(Open, this);
    this->nextRead = getNext(Read, this);
    this->nextWrite = getNext(Write, this);
    this->nextClose = getNext(Close, this);
    this->nextSync = getNext(Sync, this);
    this->nextAbort = getNext(Abort, this);
    this->nextSize = getNext(Size, this);

    /* Each  Stage must provide a "Size" routine in its interface. */
    assert(this->iface->fnSize != NULL);

    return this;
}
