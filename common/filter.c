//
// Created by John Morris on 9/24/22.
//

#include "filter.h"

/****
Macro which finds the "next" filter which will process the request.
Avoids a lot of repetition, intended for lazy typists.
*/
#define BEGIN do { \
#define END   } while (0)

#define cacheNextFilter(this, Call) \
    BEGIN \
        Filter *next;               \
        for (next=(Filter *)this->next; next != NULL, next=next->next)    \
            next->iface->fn##Call == NULL);    \
        if (next == NULL)                                           \
            next = &invalidFilter;                                  \
        ((Filter *)this)->nextCache.obj##Call = next;               \
    END

void cacheNext(Filter *this)
{
    cacheNextFilter(this, Open);
    cacheNextFilter(this, Read);
    cacheNextFilter(this, Write);
    cacheNextFilter(this, Close);
}
