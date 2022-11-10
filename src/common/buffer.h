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

    Byte *current;    /* Points to current read position in buffer, */
    Byte *endData;    /* Points to the end of valid data, where size = endData - beginData. */
} Buffer;

inline static size_t sizeMin(size_t size1, size_t size2) {return (size1<size2)? size1: size2;}
inline static size_t sizeMax(size_t size1, size_t size2) {return (size1>size2)? size1: size2;}
inline static size_t sizeRoundDown(size_t size, size_t itemSize) {return size / itemSize * itemSize;}
inline static size_t sizeRoundUp(size_t size, size_t itemSize) {return sizeRoundDown(size+itemSize-1, itemSize);}

/** Total size of buffer */
inline static size_t bufferSize(Buffer *this) {return this->endBuf - this->beginBuf;}

/** Space available to add data to buffer */
inline static size_t bufferAvailSize(Buffer *this) {return this->endBuf - this->endData;}

/** How much data remains unread */
inline static size_t bufferReadSize(Buffer *this){return this->endData - this->current;}

/** How much data is in the buffer */
inline static size_t bufferDataSize(Buffer *this){return this->endData - this->beginBuf;}


inline static bool bufferEnd(Buffer *this) {return bufferReadSize(this) == 0;}
inline static bool bufferIsFull(Buffer *this) {return bufferAvailSize(this) == 0;}
inline static bool bufferIsEmpty(Buffer *this) {return bufferReadSize(this) == 0;}

inline static bool bufferValid(Buffer *this)
    {return this->endBuf >= this->endData && this->endData >= this->current && this->current >= this->beginBuf;}

inline static void
bufferReset(Buffer *this)
{
    this->current = this->endData = this->beginBuf;
}

inline static size_t toBuffer(Buffer *this, size_t size)
{
    size_t actual = sizeMin(size, bufferAvailSize(this));
    this->endData += actual;
    return actual;
}

inline static size_t fromBuffer(Buffer *this, size_t size)
{
    size_t actual = sizeMin(size, bufferReadSize(this));
    this->current += actual;
    if (bufferIsEmpty(this))
        bufferReset(this);
    return actual;
}

inline static size_t
copyBytes(Byte *to, Byte *from, size_t size) { memcpy(to, from, size); return size; }

inline static size_t
copyFromBuffer(Buffer *this, Byte *buf, size_t size)
{
    return copyBytes(buf, this->current, fromBuffer(this, size)); // TODO: race condition on this->current?
}

inline static size_t
appendToBuffer(Buffer *this, Byte *buf, size_t size)
{
    Byte *to = this->endData;
    return copyBytes(to, buf, toBuffer(this, size));
}

inline static size_t
copyIntoBuffer(Buffer *this, Byte *buf, size_t size)
{
    Byte *to = this->current;
    size_t actual = sizeMin( this->endBuf - this->current, size);
    copyBytes(to, buf, actual);

    /* Update the pointers to reflect the data we copied into the buffer. */
    this->current += actual;
    if (this->current > this->endData)
        this->endData = this->current;

    assert(bufferValid(this));
    return actual;
}

inline static Byte
readByte(Buffer *this)
{
    assert(!bufferIsEmpty(this));
    Byte byte = *this->current++;
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
