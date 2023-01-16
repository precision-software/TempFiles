/**
 * In a pipeline of filters, these functions invoke the next
 * filter which is able to process the event.
 */
#include "iostack_error.h"
#include "assert.h"
#include "common/passThrough.h"
#include "common/packed.h"

static Error notInSink =
        (Error){.code=errorCodeIoStack, .msg="Request is not implemented for Sink", .causedBy=NULL};


/**
 * Helper to repeatedly write to the file pipeline until all the data is written (or error).
 */
size_t passThroughWriteAll(void *thisVoid, const Byte *buf, size_t bufSize, Error *error)
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
 * Our buffer must be prepared to read at least *maxReadPosition* bytes, so we stop when the remaining buffer is too small to hold them.
 */
size_t passThroughReadAll(void *thisVoid, Byte *buf, size_t size, Error *error)
{
    Filter *this = (Filter*)thisVoid;

    /* Start out empty, and count the bytes as we read them. */
    size_t totalSize = 0;

    /* Repeat until all the bytes are read (or EOF) */
    while (size > 0 && errorIsOK(*error))
    {
        /* Issue the next read, exiting on error or eof. Note size must be >= this->maxReadPosition. */
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


/*
 * Read a variable size block.
 */
size_t passThroughReadSized(void *this, Byte *block, size_t size, Error *error)
{
    /* Read the block length  TODO: create passThroughGet4(..) */
    size_t blockSize = passThroughGet4(this, error);
    if (isError(*error))
        return 0;
    if (blockSize > size)
        return ioStackError(error, "ReadSized: Block length is too large");

    /* Read the rest of the block */
    size_t actual = passThroughReadAll(this, block, blockSize, error);

    /* Done. actual will match encryptSize, unless there was an error. */
    return actual;
}

size_t passThroughWriteSized(void *this, Byte *block, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    /* Write out the 32-bit size in network byte order (big endian) */
    passThroughPut4(this, size, error);

    /* Write out the block */
    return passThroughWriteAll(this, block, size, error);
}


/**
 * A Dummy size function, just as a placeholder.
 */
size_t dummyBlockSize(Filter *this, size_t size, Error *error)
{
    return passThroughBlockSize(this, size, error);
}

/*
 * Defines a "no-op" filter, mainly to use as a placeholder.
 */
FilterInterface passThroughInterface = (FilterInterface) {.fnBlockSize = (FilterBlockSize)dummyBlockSize};
