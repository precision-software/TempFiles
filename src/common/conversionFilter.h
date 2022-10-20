//
// Created by John Morris on 10/18/22.
//

#ifndef FILTER_CONVERSIONFILTER_H
#define FILTER_CONVERSIONFILTER_H

#include "common/filter.h"

typedef struct ConverterIface
{
    void (*beginFrame) (void *this, Error *error);  // Not needed?  merged into "new"?  (matters when converting multiple files ...)
    void (*convert) (void *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error);
    void (*endFrame) (void *this, Error *error);    // Not needed? merged into "free"?
    void (*free) (void *this, Error *error);
    size_t (*estimateToSize)(size_t fromSize);
} ConverterIface;

typedef struct Converter
{
    ConverterIface *iface;
    void *converter;
} Converter;

static inline void converterBeginFrame(Converter *this, Error *error) {this->iface->beginFrame(this->converter, error);}

static inline void
converterConvert(Converter *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error)
{
    return this->iface->convert(this->converter, toBuf, toSize, fromBuf, fromSize, error);
}

static inline void converterEndFrame(Converter *this, Error *error)
{
    return this->iface->endFrame(this->converter, error);
}

static inline void converterFree(Converter *this, Error *error)
{
    return this->iface->free(this, error);
}

typedef struct ConversionFilter ConversionFilter;
Filter *conversionFilterNew(Filter *next, size_t blockSize, Converter *(*newTo)(void *confg), Converter *(newFrom)(void *config));

#endif //FILTER_CONVERSIONFILTER_H
