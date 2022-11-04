/**
 * Converters transform a chunks of data from one form to another.
 * They are typically used compress or encrypt a stream of data.
 * In the "pipeline of filters" approach, the "ConvertFilter" is a special
 * filter which holds two converters, one for reading and another for writing.
 *
 * A Converter has the following lifecycle:
 *    converterNew() - Create an abstract Converter object.
 *    converterBegin() - Initialize the Converter and place any header info in the out buffer.
 *    converterProcess() - Process the next chunk of bytes, placing them in the out buffer.
 *    converterEnd() - Place any final bytes in the out buffer.
 *    converterFree() - Release all resources
 *
 * Converters also implement the converterSize() method, which estimates how
 * much output can be produced by a given input. The output buffer must always
 * have that much space available to receive output.
 *
 * Some error handling is implemented in the abstract wrappers, so the
 * individual converters don't have test errors or clean up their output
 * when an error occurs.
 */
#ifndef CONVERTER_H
#define CONVERTER_H

#include "common/error.h"

/*
 * These are the "sub-events" which are handled by converters.
 */
typedef size_t (*ConvertBeginFn) (void *this, Byte *buf, size_t bufSize, Error *error);
typedef size_t (*ConvertSizeFn)(void *this, size_t size);
typedef void (*ConvertConvertFn)(void *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error);
typedef size_t (*ConvertEndFn)(void *this, Byte *toBuf, size_t toSize, Error *error);
typedef void (*ConvertFreeFn)(void *this, Error *error);

typedef struct ConverterIface
{
    ConvertSizeFn fnSize;
    ConvertBeginFn fnBegin;
    ConvertConvertFn fnConvert;
    ConvertEndFn fnEnd;
    ConvertFreeFn fnFree;
} ConverterIface;

typedef struct Converter
{
    ConverterIface *iface;
    void *converter;
} Converter;

static inline Converter *converterNew(void *converter, ConverterIface *iface)
{
    Converter *this = malloc(sizeof(Converter));
    *this = (Converter) {.converter = converter, .iface = iface};
    return this;
}

static inline size_t converterBegin(Converter *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    return this->iface->fnBegin(this->converter, buf, size, error);
}

static inline size_t converterSize(Converter *this, size_t fromSize)
{
    return this->iface->fnSize(this->converter, fromSize);
}


static inline void
converterProcess(Converter *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error)
{
    if (!isError(*error))
        this->iface->fnConvert(this->converter, toBuf, toSize, fromBuf, fromSize, error);

    if (isError(*error)) {
        *toSize = 0;
        *fromSize = 0;
    }
}


static inline size_t converterEnd(Converter *this, Byte *toBuf, size_t toSize, Error *error)
{
    if (isError(*error))
        return 0;

    return this->iface->fnEnd(this->converter, toBuf, toSize, error);
}

static inline void converterFree(Converter *this, Error *error)
{
    return this->iface->fnFree(this, error);
}



#endif /*CONVERTER_H */
