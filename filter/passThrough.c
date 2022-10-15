//
// Created by John Morris on 10/10/22.
//
#include "error.h"
#include "passThrough.h"

static Error notInSink =
        (Error){.code=errorCodeFilter, .msg="Request is not implemented for Sink", .causedBy=NULL};

/****
* Pass Through filter which does nothing.
*/
#define getNextFn(this, request, fn, error) \
    Filter##request fn;\
    do {                             \
        this = ((Filter*)this)->next;\
        fn = ((Filter*)this)->iface->fn##request;  \
    } while (this != NULL && fn == NULL);          \
    if (fn == NULL)                  \
        error = notInSink; \
    else (void)0

// Declare pass through functions for each type of request.
Error passThroughOpen(void *this, char *path, int mode, int perm)
{
    Error error = errorOK;
    getNextFn(this, Open, fn, error);
    if (errorIsOK(error))
        error = fn(this, path, mode, perm);
    return error;
}

size_t passThroughRead(void *this, Byte *buf, size_t size, Error *error)
{
    getNextFn(this, Read, fn, *error);
    if (!errorIsOK(*error))
        return 0;
    return fn(this, buf, size, error);
}

size_t passThroughWrite(void *this, Byte *buf, size_t size, Error *error)
{
    getNextFn(this, Read, fn, *error);
    if (!errorIsOK(*error))
        return 0;
    return fn(this, buf, size, error);
}

void passThroughClose(void *this, Error *error)
{
    getNextFn(this, Close, fn, *error);
    fn(this, error);
}


/***********************************************************************************************************************************
Helper to repeatedly write to the next filter in the pipeline until all the data is written (or error).
***********************************************************************************************************************************/
size_t passThroughWriteAll(void *this, Byte *buf, size_t size, Error *error)
{
    // Start out empty, but count the bytes as we write them out.
    size_t totalSize = 0;

    // Repeat until all the bytes are written.
    do {
        // Issue the next write, exiting on error.
        size_t actualSize = passThroughWrite(this, buf, size, error);
        if (!errorIsOK(*error))
            break;

        // Update the bytes transferred so far.
        buf += actualSize;
        size -= actualSize;
        totalSize += actualSize;

    // End "Repeat until all the bytes are written"
    } while (size > 0);

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
    do {
        // Issue the next read, exiting on error or eof.
        size_t actualSize = passThroughRead(this, buf, size, error);
        if (!errorIsOK(*error))
            break;

        // Update the bytes transferred so far.
        buf += actualSize;
        size -= actualSize;
        totalSize += actualSize;

    // End "Repeat until all the bytes are read ..."
    } while (size > 0);

    // If last read had eof, but we read data earlier, then all is OK. We'll get another eof next read.
    if (errorIsEOF(*error) && totalSize > 0)
        *error = errorOK;

    return totalSize;
}

/***********************************************************************************************************************************
Defines a "no-op" filter, mainly to use as a placeholder.
***********************************************************************************************************************************/
FilterInterface passThroughInterface = (FilterInterface) {0};
