//
// Created by John Morris on 10/10/22.
//
#include "common/error.h"
#include "assert.h"
#include "passThrough.h"

static Error notInSink =
        (Error){.code=errorCodeFilter, .msg="Request is not implemented for Sink", .causedBy=NULL};

/****
PassThrough macro which finds "next" the following filter which will process the request
Avoids a lot of repetition, intended for lazy typists.
*/
#define passThroughGetNext(call, error, errorReturn) \
    if (isError(error)) return errorReturn;  \
    Filter *next = (Filter*)this;\
    do {\
        next = next->next; \
    } while (next != NULL && next->iface->fn##call == NULL); \
    if (next == NULL) { \
        error = notInSink; \
        return errorReturn; \
    }

// Declare pass through functions for each type of request.
Error passThroughOpen(void *this, char *path, int mode, int perm)
{
    Error error = errorOK;
    passThroughGetNext(Open, error, error);
    return next->iface->fnOpen(next, path, mode, perm);
}

size_t passThroughRead(void *this, Byte *buf, size_t size, Error *error)
{
    passThroughGetNext(Read, *error, 0);
    return next->iface->fnRead(next, buf, size, error);
}

size_t passThroughWrite(void *this, Byte *buf, size_t size, Error *error)
{
    passThroughGetNext(Write, *error, 0);
    size_t actual = next->iface->fnWrite(next, buf, size, error);
    assert(actual <= size);
    return actual;
}

void passThroughClose(void *this, Error *error)
{
    passThroughGetNext(Close, *error, (void)0);
    return next->iface->fnClose(next, error);
}


/***********************************************************************************************************************************
Helper to repeatedly write to the next filter in the pipeline until all the data is written (or error).
***********************************************************************************************************************************/
size_t passThroughWriteAll(void *this, Byte *buf, size_t bufSize, Error *error)
{
    assert ((ssize_t)bufSize > 0);
    if (isError(*error))
        return 0;

    // Start out empty, but count the bytes as we write them out.
    size_t totalSize = 0;

    // Repeat until all the bytes are written.
    do {
        // Issue the next write, exiting on error.
        size_t actualSize = passThroughWrite(this, buf, bufSize, error);
        if (isError(*error))
            break;

        // Update the bytes transferred so far.
        buf += actualSize;
        bufSize -= actualSize;
        totalSize += actualSize;

    // End "Repeat until all the bytes are written"
    } while (bufSize > 0);

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
        if (isError(*error))
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
