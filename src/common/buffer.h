/* */
/* Created by John Morris on 10/11/22. */
/* */

#ifndef FILTER_BUFFER_H
#define FILTER_BUFFER_H


#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stdbool.h>
#include "error.h"

/* A generic data buffer. */
typedef struct Buffer
{
    Byte *beginBuf;   /* The beginning of the buffer */
    Byte *endBuf;     /* The end of the buffer, where size = endBuf - beginBuf */

    Byte *beginData;  /* Points to the beginning of valid data. */
    Byte *endData;    /* Points to the end of valid data, where size = endData - beginData. */
} Buffer;

inline static size_t sizeMin(size_t size1, size_t size2) {return (size1<size2)? size1: size2;}
inline static size_t sizeMax(size_t size1, size_t size2) {return (size1>size2)? size1: size2;}
inline static size_t sizeRoundDown(size_t size, size_t itemSize) {return size / itemSize * itemSize;}
inline static size_t sizeRoundUp(size_t size, size_t itemSize) {return sizeRoundDown(size+itemSize-1, itemSize);}

inline static size_t bufferDataSize(Buffer *this){return this->endData - this->beginData;}
inline static size_t bufferSize(Buffer *this) {return this->endBuf - this->beginBuf;}

inline static size_t bufferAvailSize(Buffer *this) {return this->endBuf - this->endData;}

inline static bool bufferIsFull(Buffer *this) {return bufferAvailSize(this) == 0;}
inline static bool bufferIsEmpty(Buffer *this) {return bufferDataSize(this) == 0;}

inline static bool bufferValid(Buffer *this)
    {return this->endBuf >= this->endData && this->endData >= this->beginData && this->beginData >= this->beginBuf;}

inline static void
bufferReset(Buffer *this)
{
    this->beginData = this->endData = this->beginBuf;
}

inline static size_t toBuffer(Buffer *this, size_t size)
{
    size_t actual = sizeMin(size, bufferAvailSize(this));
    this->endData += actual;
    return actual;
}

inline static size_t fromBuffer(Buffer *this, size_t size)
{
    size_t actual = sizeMin(size, bufferDataSize(this));
    this->beginData += actual;
    if (bufferIsEmpty(this))
        bufferReset(this);
    return actual;
}

inline static size_t
copyBytes(Byte *to, Byte *from, size_t size) { memcpy(to, from, size); return size; }

inline static size_t
copyFromBuffer(Buffer *this, Byte *buf, size_t size)
{
    Byte *from = this->beginData;
    return copyBytes(buf, from, fromBuffer(this, size));
}

inline static size_t
copyToBuffer(Buffer *this, Byte *buf, size_t size)
{
    Byte *to = this->endData;
    return copyBytes(to, buf, toBuffer(this, size));
}

inline static Byte
readByte(Buffer *this)
{
    assert(!bufferIsEmpty(this));
    Byte byte = *this->beginData++;
    if (bufferIsEmpty(this))
        bufferReset(this);
    return byte;
}

inline static void
writeByte(Buffer *this, Byte byte)
{
    assert(!bufferIsFull(this));
    *this->endData++ = byte;
}


Buffer *bufferNew(size_t size);
void bufferFree(Buffer *this);
void bufferForceFlush(Buffer *this, void *filter, Error *error);
void bufferFlush(Buffer *this, void *filter, Error *error);
void bufferFill(Buffer *this, void *filter, Error *error);

#endif /*FILTER_BUFFER_H */
