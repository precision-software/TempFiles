/**
 *
 */
#include "error.h"
#include "assert.h"
#include "passThrough.h"

static Error notInSink =
        (Error){.code=errorCodeFilter, .msg="Request is not implemented for Sink", .causedBy=NULL};


/**
 * Helper to repeatedly write to the next filter in the pipeline until all the data is written (or error).
 */
size_t passThroughWriteAll(void *thisVoid, Byte *buf, size_t bufSize, Error *error)
{
    assert ((ssize_t)bufSize > 0);
    Filter *this = (Filter *)thisVoid;

    /* Start out as though empty, and then count the bytes as we write them out. */
    size_t totalSize = 0;

    /* Repeat until all the bytes are written. */
    while (bufSize > 0 && errorIsOK(*error))
    {
        /* Issue the next write, exiting on error. */
        size_t actualSize = passThroughWrite(this, buf, bufSize, error);

        /* Update the bytes transferred so far. */
        buf += actualSize;
        bufSize -= actualSize;
        totalSize += actualSize;
    }

    return totalSize;
}

/**
 * Helper to repeatedly read from the next filter in the pipeline until small amount of data left to read, eof, or error.
 * Our buffer must be prepared to read at least *readSize* bytes, so we stop when the remaining buffer is too small to hold them.
 */
size_t passThroughReadAll(void *thisVoid, Byte *buf, size_t size, Error *error)
{
    Filter *this = (Filter*)thisVoid;

    /* Start out empty, and count the bytes as we read them. */
    size_t totalSize = 0;

    /* Repeat until all the bytes are read (or EOF) */
    while (size >= this->readSize && errorIsOK(*error))
    {
        /* Issue the next read, exiting on error or eof. Note size must be >= this->readSize. */
        size_t actualSize = passThroughRead(this, buf, size, error);

        /* Update the bytes transferred so far. */
        buf += actualSize;
        size -= actualSize;
        totalSize += actualSize;
    }

    /* If last read had eof, but we were able to read some data, then all is OK. We'll get another eof next read. */
    if (errorIsEOF(*error) && totalSize > 0)
        *error = errorOK;

    return totalSize;
}

/**
 * A Dummy size function, just as a placeholder.
 */
size_t dummySize(Filter *this, size_t size)
{
    this->writeSize = size;
    this->readSize = passThroughSize(this, size);
    return this->readSize;
}

/*
 * Defines a "no-op" filter, mainly to use as a placeholder.
 */
FilterInterface passThroughInterface = (FilterInterface) {.fnSize = (FilterSize)dummySize};
