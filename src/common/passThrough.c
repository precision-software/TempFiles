/**
 * In a pipeline of filters, these functions invoke the next
 * filter which is able to process the event.
 */
#include "error.h"
#include "assert.h"
#include "common/passThrough.h"
#include "common/packed.h"

static Error notInSink =
        (Error){.code=errorCodeFilter, .msg="Request is not implemented for Sink", .causedBy=NULL};


/**
 * Helper to repeatedly write to the file pipeline until all the data is written (or error).
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
 * Read a variable size record.
 *   TODO: 1) size should not be part of the data, 2) Also implemented as a filter, 2) include size separately in AEAD validation.
 */
size_t passThroughReadSized(void *this, Byte *record, size_t size, Error *error)
{
    /* Read the record length  TODO: create passThroughGet4(..) */
    size_t recordSize = passThroughGet4(this, error);
    if (isError(*error))
        return 0;
    if (recordSize > size)
        return filterError(error, "Record length is too large");

    /* Read the rest of the record */
    size_t actual = passThroughReadAll(this, record, recordSize, error);

    /* Done. actual will match cipherSize, unless there was an error. */
    return actual;
}

size_t passThroughWriteSized(void *this, Byte *record, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    /* Write out the 32-bit size in network byte order (big endian) */
    passThroughPut4(this, size, error);

    /* Write out the record */
    return passThroughWriteAll(this, record, size, error);
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


bool passThroughPut8(void *this, size_t value, Error *error)
{
    Byte buf[8]; Byte *bp = buf;
    pack8(&bp, bp + 8, value);
    passThroughWriteAll(this, buf, 8, error);
    return bp > bp + 8;
}

size_t passThroughGet8(void *this, Error *error)
{
    Byte buf[8]; Byte *bp = buf;
    passThroughReadAll(this, buf, 8, error);

    return unpack8(&bp, buf+8);
}


bool passThroughPut4(void *this, size_t value, Error *error)
{
    Byte buf[4]; Byte *bp = buf;
    pack4(&bp, bp + 4, value);
    passThroughWriteAll(this, buf, 4, error);
    return bp > bp + 4;
}

size_t passThroughGet4(void *this, Error *error)
{
    Byte buf[4]; Byte *bp = buf;
    passThroughReadAll(this, buf, 4, error);

    return unpack4(&bp, buf+4);
}
