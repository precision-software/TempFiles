//
// Created by John Morris on 10/11/22.
//

#include "buffer.h"
#include "request.h"
#include "passThrough.h"


/***********************************************************************************************************************************
Write out all data in the buffer, even if the buffer isn't full.
***********************************************************************************************************************************/
Error bufferForceFlush(Buffer *buf, void *filter)
{
    // Assume things are good unless proven otherwise.
    Error error = errorOK;

    // If the buffer has data to write ...
    size_t remaining = buf->writePtr - buf->readPtr;
    if (remaining > 0)
    {
        // Write out all the data in the buffer.
        WriteRequest flush = (WriteRequest) {.buf=buf->readPtr, .bufSize=remaining};
        passThroughWriteAll(filter, &flush);

        // Apply any errors to the write request which caused the flush.
        error = flush.error;
    }

    // The buffer is now empty.
    bufferReset(buf);
    return error;
}

/***********************************************************************************************************************************
Write out data if the buffer is full.
***********************************************************************************************************************************/
Error bufferFlush(Buffer *this, void *filter)
{
    Error error = errorOK;

    if (bufferIsFull(this))
        error = bufferForceFlush(this, filter);

    return error;
}

/***********************************************************************************************************************************
Fill up an empty buffer. Do not overwrite existing data!
***********************************************************************************************************************************/
Error bufferFill(Buffer *this, void *filter)
{
    // Assume all is good until proven otherwise.
    Error error = errorOK;

    // If the buffer is empty,
    if (bufferIsEmpty(this))
    {
        // Read in a new buffer. Request a full block, but OK if less arrives.
        ReadRequest req = (ReadRequest) {.buf=this->writePtr, .bufSize=this->endPtr - this->writePtr};
        passThroughRead(filter, &req);
        if (errorIsOK(req.error))
            this->writePtr += req.actualSize;

        // If any errors, apply them to the read request which triggered this read.
        error = req.error;
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
