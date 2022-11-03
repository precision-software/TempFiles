/* */
/* Created by John Morris on 10/11/22. */
/* */

#include "common/buffer.h"
#include "common/passThrough.h"


/***********************************************************************************************************************************
Write out all data in the buffer, even if the buffer isn't full.
***********************************************************************************************************************************/
void bufferForceFlush(Buffer *buf, void *filter, Error *error) /* TODO: pass *error as a param, check + return. */
{
    /* If the buffer has data to write ... */
    size_t remaining = bufferDataSize(buf);
    if (remaining > 0)
        passThroughWriteAll(filter, buf->beginData, remaining, error);

    bufferReset(buf);
}

/***********************************************************************************************************************************
Write out data if the buffer is full.
***********************************************************************************************************************************/
void bufferFlush(Buffer *this, void *filter, Error *error)  /* TODO pass error as a parameter ahd test. */
{
    if (bufferIsFull(this))
        bufferForceFlush(this, filter, error);
}

/***********************************************************************************************************************************
Fill up an empty buffer. Do not overwrite existing data!
***********************************************************************************************************************************/
void bufferFill(Buffer *this, void *filterVoid, Error *error)  /* TODO: pass error and test first. */
{
    Filter *filter = filterVoid;

    /* If the buffer is empty, */
    if (bufferIsEmpty(this))
    {
        /* Read in a new buffer. Request a full buffer, but OK if less arrives. */
        size_t actualSize = passThroughRead(filter, this->endData, this->endBuf - this->endData, error);
        if (errorIsOK(*error))
            this->endData += actualSize;
    }
}

Buffer *
bufferNew(size_t size)
{
    /* Allocate memory for the buffer structure. */
    Buffer *this = malloc(sizeof(Buffer));
    this->beginBuf = malloc(size);

    /* Set up the internal pointers for an empty buffer. */
    this->endBuf = this->beginBuf + size;
    bufferReset(this);

    return this;
}

void
bufferFree(Buffer *this)
{
    free(this->beginBuf);
    free(this);
}

void
bufferResize(Buffer *this, size_t size)
{
    /* TODO; */
}
