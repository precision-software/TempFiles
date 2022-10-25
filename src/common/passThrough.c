//
// Created by John Morris on 10/10/22.
//
#include "error.h"
#include "assert.h"
#include "passThrough.h"

static Error notInSink =
        (Error){.code=errorCodeFilter, .msg="Request is not implemented for Sink", .causedBy=NULL};


/***********************************************************************************************************************************
Helper to repeatedly write to the next filter in the pipeline until all the data is written (or error).
***********************************************************************************************************************************/
size_t passThroughWriteAll(void *this, Byte *buf, size_t bufSize, Error *error)
{
    assert ((ssize_t)bufSize > 0);

    // Start out empty, but count the bytes as we write them out.
    size_t totalSize = 0;

    // Repeat until all the bytes are written.
    while (bufSize > 0 && errorIsOK(*error))
    {
        // Issue the next write, exiting on error.
        size_t actualSize = passThroughWrite(this, buf, bufSize, error);

        // Update the bytes transferred so far.
        buf += actualSize;
        bufSize -= actualSize;
        totalSize += actualSize;
    }

    return totalSize;
}

/***********************************************************************************************************************************
Helper to repeatedly read from the next filter in the pipeline until all the data is read, eof, or error.
***********************************************************************************************************************************/
size_t passThroughReadAll(void *this, Byte *buf, size_t size, Error *error)
{
    // Start out empty, but count the bytes as we read them.
    size_t totalSize = 0;

    // Repeat until all the bytes are read (or EOF)
    while (size > 0 && errorIsOK(*error))
    {
        // Issue the next read, exiting on error or eof.
        size_t actualSize = passThroughRead(this, buf, size, error);

        // Update the bytes transferred so far.
        buf += actualSize;
        size -= actualSize;
        totalSize += actualSize;
    }

    // If last read had eof, but we were able to read some data, then all is OK. We'll get another eof next read.
    if (errorIsEOF(*error) && totalSize > 0)
        *error = errorOK;

    return totalSize;
}

/***********************************************************************************************************************************
Defines a "no-op" filter, mainly to use as a placeholder.
***********************************************************************************************************************************/
FilterInterface passThroughInterface = (FilterInterface) {0};
