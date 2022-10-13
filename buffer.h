//
// Created by John Morris on 10/11/22.
//

#ifndef FILTER_BUFFER_H
#define FILTER_BUFFER_H


#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stdbool.h>
#include "error.h"

typedef unsigned char Byte;

// A generic data buffer.
typedef struct Buffer
{
    Byte *buf;                                                      // The data buffer itself.
    Byte *endPtr;                                                   // The end of the buffer, where size = endPtr-buf;

    Byte *readPtr;                                                  // Points to the next byte to be read from buffer.
    Byte *writePtr;                                                 // Points to the last byte written to the buffer.
} Buffer;

inline static size_t sizeMin(size_t size1, size_t size2) {return (size1<size2)? size1: size2;}

inline static bool bufferIsEmpty(Buffer *this) {return this->readPtr == this->writePtr;}
inline static bool bufferIsFull(Buffer *this) {return this->writePtr == this->endPtr;}
inline static bool bufferIsDirty(Buffer *this) {return this->writePtr > this->readPtr;}

inline static void
bufferReset(Buffer *this)
{
    this->readPtr = this->writePtr = this->buf;
}

inline static size_t
readFromBuffer(Buffer *this, Byte *buf, size_t bufSize)
{
    size_t size = sizeMin(bufSize, this->writePtr - this->readPtr);
    memcpy(buf, this->readPtr, size);
    this->readPtr += size;

    if (bufferIsEmpty(this))
        bufferReset(this);

    return size;
}

inline static size_t
writeToBuffer(Buffer *this, Byte *buf, size_t bufSize)
{
    assert(!bufferIsFull(this));
    size_t size = sizeMin(bufSize, this->endPtr - this->writePtr);
    memcpy(this->writePtr, buf, size);
    this->writePtr += size;
    return size;
}

inline static Byte
readByte(Buffer *this)
{
    assert(!bufferIsEmpty(this));
    Byte byte = *this->readPtr++;
    if (bufferIsEmpty(this))
        bufferReset(this);
    return byte;
}

inline static void
writeByte(Buffer *this, Byte byte)
{
    assert(!bufferIsFull(this));
    *this->writePtr++ = byte;
}


Buffer *bufferNew(size_t size);
void bufferFree(Buffer *this);
Error bufferForceFlush(Buffer *this, void *filter);
Error bufferFlush(Buffer *this, void *filter);
Error bufferFill(Buffer *this, void *filter);

#endif //FILTER_BUFFER_H
