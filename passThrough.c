//
// Created by John Morris on 10/10/22.
//
#include "error.h"
#include "passThrough.h"

static const Error notInSink = (Error){.code=errorCodeFilter, .msg="Request is not implemented for Sink", .causedBy=NULL};

/****
* Pass Through filter which does nothing.
*/
#define passThrough(RequestType)                                                                                                   \
    void passThrough##RequestType(void *filter, RequestType##Request *request)                                                     \
    {    Filter *this = filter;                                                                                                    \
         if (this->next == NULL)                                                                                                   \
             request->error = notInSink;                                                                                           \
         else                                                                                                                      \
             this->next->iface->fn##RequestType(this->next, request);                                                              \
    }

// Declare pass through functions for each type of request.
passThrough(Abort)
passThrough(Open)
passThrough(Read)
passThrough(Write)
passThrough(Seek)
passThrough(Sync)
passThrough(Close)
passThrough(Peek)

/***********************************************************************************************************************************
Helper to repeatedly write to the next filter in the pipeline until all the data is written (or error).
***********************************************************************************************************************************/
void passThroughWriteAll(void *this, WriteRequest *req)
{
    // Start out empty, but count the bytes as we write them out.
    req->actualSize = 0;

    // Repeat until all the bytes are written.
    WriteRequest sub = {.buf=req->buf, .bufSize=req->bufSize};
    do {
        // Issue the next write, exiting on error.
        passThroughWrite(this, &sub);
        if (!errorIsOK(sub.error))
            break;

        // Update the bytes transferred so far.
        sub.buf += sub.actualSize;
        sub.bufSize -= sub.actualSize;
        req->actualSize += sub.actualSize;

    // End "Repeat until all the bytes are written"
    } while (sub.bufSize > 0);

    // If there was an error, pass it on.
    req->error = sub.error;
}

/***********************************************************************************************************************************
Helper to repeatedly read from the next filter in the pipeline until all the data is read, eof, or error.
***********************************************************************************************************************************/
void passThroughReadAll(void *this, ReadRequest *req)
{
    // Start out empty, but count the bytes as we read them.
    req->actualSize = 0;

    // Repeat until all the bytes are read (or EOF)
    ReadRequest sub = {.buf=req->buf, .bufSize=req->bufSize};
    do {
        // Issue the next read, exiting on error or eof.
        passThroughRead(this, &sub);
        if (!errorIsOK(sub.error))
            break;

        // Update the bytes transferred so far.
        sub.buf += sub.actualSize;
        sub.bufSize -= sub.actualSize;
        req->actualSize += sub.actualSize;

    // End "Repeat until all the bytes are read ..."
    } while (sub.bufSize > 0);

    // If last read had eof, but we read data earlier, then all is OK. We'll get another eof next read.
    if (errorIsEOF(sub.error) && req->actualSize > 0)
        req->error = errorOK;
    else
        req->error = sub.error;
}

/***********************************************************************************************************************************
Defines a "no-op" filter, mainly to use as a placeholder.
***********************************************************************************************************************************/
FilterInterface passThroughInterface = (FilterInterface) {
    .fnSync = (FilterService) passThroughSync,
    .fnAbort = (FilterService) passThroughAbort,
    .fnClose = (FilterService) passThroughClose,
    .fnOpen = (FilterService) passThroughOpen,
    .fnPeek = (FilterService) passThroughPeek,
    .fnRead = (FilterService) passThroughRead,
    .fnWrite = (FilterService) passThroughWrite,
    .fnSeek = (FilterService) passThroughSeek
};
