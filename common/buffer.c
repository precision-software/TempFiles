//
// Created by John Morris on 10/11/22.
//

#include "buffer.h"
#include "request.h"
#include "passThrough.h"


/***********************************************************************************************************************************
Write out all data in the buffer, even if the buffer isn't full.
***********************************************************************************************************************************/
Error bufferForceFlush(Buffer *buf, void *filter) // TODO: pass *error as a param, check + return.
{
    // Assume things are good unless proven otherwise.
    Error error = errorOK;

    // If the buffer has data to write ...
    size_t remaining = buf->writePtr - buf->readPtr;
    if (remaining > 0)
        passThroughWriteAll(filter, buf->readPtr, remaining, &error);

    bufferReset(buf);

    return error;
}

/***********************************************************************************************************************************
Write out data if the buffer is full.
***********************************************************************************************************************************/
Error bufferFlush(Buffer *this, void *filter)  // TODO pass error as a parameter ahd test.
{
    Error error = errorOK;

    if (bufferIsFull(this))
        error = bufferForceFlush(this, filter);

    return error;
}

/***********************************************************************************************************************************
Fill up an empty buffer. Do not overwrite existing data!
***********************************************************************************************************************************/
Error bufferFill(Buffer *this, void *filter)  // TODO: pass error and test first.
{
    // Assume all is good until proven otherwise.
    Error error = errorOK;

    // If the buffer is empty,
    if (bufferIsEmpty(this))
    {
        // Read in a new buffer. Request a full buffer, but OK if less arrives.
        size_t actualSize = passThroughRead(filter, this->writePtr, this->endPtr - this->writePtr, &error);
        if (errorIsOK(error))
            this->writePtr += actualSize;
    }

    return error;
}

Buffer *
bufferNew(size_t size)
{
    // Allocate memory for the buffer structure.
    Buffer *this = malloc(sizeof(Buffer));
    this->buf = malloc(size);

    // Set up the internal pointers for an empty buffer.
    this->endPtr = this->buf + size;
    bufferReset(this);

    return this;
}

void
bufferFree(Buffer *this)
{
    free(this->buf);
    free(this);
}

void
bufferResize(Buffer *this, size_t size)
{
    // TODO;
}
