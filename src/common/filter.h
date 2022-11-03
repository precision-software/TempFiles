/**
 * Filters transform data from one buffer to another. They have the follow lifecycle:
 *    filterNew() - Create an abstract Filter object.
 *    filterBegin - Initialize the Filter.
 *    filterProcess - Process bytes from the in buffer, placing bytes in the out buffer.
 *    filterEnd - Place any final bytes in the out buffer.
 *    filterFree - Release all resources
 */

#ifndef FILTER_H
#define FILTER_H

#include "common/error.h"


typedef size_t (*ConvertBeginFn) (void *this, Byte *buf, size_t bufSize, Error *error);
typedef size_t (*ConvertSizeFn)(void *this, size_t size);
typedef void (*ConvertConvertFn)(void *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error);
typedef size_t (*ConvertEndFn)(void *this, Byte *toBuf, size_t toSize, Error *error);
typedef void (*ConvertFreeFn)(void *this, Error *error);

typedef struct FilterIface
{
    ConvertSizeFn fnSize;
    ConvertBeginFn fnBegin;
    ConvertConvertFn fnConvert;
    ConvertEndFn fnEnd;
    ConvertFreeFn fnFree;
} FilterIface;

typedef struct Filter
{
    FilterIface *iface;
    void *filter;
} Filter;

static inline Filter *filterNew(void *filter, FilterIface *iface)
{
    Filter *this = malloc(sizeof(Filter));
    *this = (Filter) {.filter = filter, .iface = iface};
    return this;
}

static inline size_t filterBegin(Filter *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    return this->iface->fnBegin(this->filter, buf, size, error);
}

static inline size_t filterSize(Filter *this, size_t fromSize)
{
    return this->iface->fnSize(this->filter, fromSize);
}


static inline void
filterProcess(Filter *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error)
{
    if (!isError(*error))
        this->iface->fnConvert(this->filter, toBuf, toSize, fromBuf, fromSize, error);

    if (isError(*error)) {
        *toSize = 0;
        *fromSize = 0;
    }
}


static inline size_t filterEnd(Filter *this, Byte *toBuf, size_t toSize, Error *error)
{
    if (isError(*error))
        return 0;

    return this->iface->fnEnd(this->filter, toBuf, toSize, error);
}

static inline void converterFree(Converter *this, Error *error)
{
    return this->iface->fnFree(this, error);
}



#endif /*CONVERTER_H */
