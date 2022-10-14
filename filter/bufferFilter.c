/***********************************************************************************************************************************

***********************************************************************************************************************************/
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include "error.h"
#include "buffer.h"
#include "passThrough.h"

#include "bufferFilter.h"

#define palloc malloc

/***********************************************************************************************************************************
Filter which reconciles an input of one block size with output of a different block size.
It replicates the functionality of fread/fwrite/fseek, but with an added "Peek" interface which
can minimize the overhead of copying data.
***********************************************************************************************************************************/
struct BufferFilter
{
    Filter header;                                                  /* Common to all filters */
    size_t blockSize;                                               /* Block size of the next filter in the pipeline. */
    Buffer *buf;                                                    /* Local buffer */
    bool readable;
    bool writeable;
};

Error errorCantBothReadWrite =
        (Error) {.code=errorCodeFilter, .msg="BufferFilter can't read and write the same stream", .causedBy=NULL};

void
bufferFilterOpen(BufferFilter *this, OpenRequest *req)
{
    // We support I/O in only one direction per open.
    this->readable = (req->mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (req->mode & O_ACCMODE) != O_RDONLY;
    if (this->readable && this->writeable)
    {
        req->error = errorCantBothReadWrite;
        return;
    }

    // Pass the open request to the next filter and get the response.
    passThroughOpen(this, req);

    // If Successful, get the blockSize and finish initializing.
    if (errorIsOK(req->error)) {
        this->blockSize = req->blockSize;
        this->buf = bufferNew(this->blockSize);
        req->blockSize = 1;
    }
}


void bufferFilterWrite(BufferFilter *this, WriteRequest *req)
{
    // If our buffer is empty and our write is larger than a single block, avoid the capy and pass it on directly.
    if (bufferIsEmpty(this->buf) && req->bufSize >= this->blockSize)
        passThroughWrite(this, req);

    // otherwise, transfer using the buffer.
    else
    {
        // Copy data into the buffer.
        req->actualSize = writeToBuffer(this->buf, req->buf, req->bufSize);

        // Flush the buffer if full, attributing any errors to our write request.
        req->error = bufferFlush(this->buf, this);
    }
}

void bufferFilterRead(BufferFilter *this, ReadRequest *req)
{
    // If our buffer is empty and our read is more than a buffer, avoid the capy and pass it on directly.
    if (bufferIsEmpty(this->buf) && req->bufSize >= this->blockSize)
        passThroughRead(this, req);

    // otherwise, transfer using the buffer.
    else
    {
        // If buffer is empty, fetch some more data, attributing errors to our read request.
        req->error = bufferFill(this->buf, this);

        // Copy data out of the buffer if all is OK.
        if (errorIsOK(req->error))
            req->actualSize = readFromBuffer(this->buf, req->buf, req->bufSize);
    }
}

// Allow others to directly access our buffer to minimize copying of data.
void bufferFilterPeek(BufferFilter *this, PeekRequest *req)
{
    // We now have a pointer to the previous node's buffer.
    Buffer *srcBuf = req->srcBuf;

    // Request a pointer to the buffer in the next node, giving them ours in turn.
    req->srcBuf = this->buf;
    passThroughPeek(this, req);
    Buffer *sinkBuf = req->sinkBuf;

    // Reply with our buffer to the preceding node.
    req->sinkBuf = this->buf;
    req->error = errorOK;
}

void bufferFilterSeek(BufferFilter *this, SeekRequest *req)
{
}

void bufferFilterSync(BufferFilter *this, SyncRequest *req)
{
}


void bufferFilterClose(BufferFilter *this, CloseRequest *req)
{
    // Flush our buffers.
    Error flushError = bufferForceFlush(this->buf, this);

    // Pass on the close request
    passThroughClose(this, req);

    // but give priority to the flush error.
    if (!errorIsOK(flushError))
        req->error = flushError;

    // Free up the buffer.
    bufferFree(this->buf);
    this->buf = NULL;
}

void bufferFilterAbort(BufferFilter *this, AbortRequest *req)
{
}


FilterInterface bufferFilterInterface = (FilterInterface)
{
    .fnOpen = (FilterService)bufferFilterOpen,
    .fnWrite = (FilterService)bufferFilterWrite,
    .fnClose = (FilterService)bufferFilterClose,
    .fnRead = (FilterService)bufferFilterRead,

    // Not implemented.
    .fnAbort = (FilterService)passThroughAbort,
    .fnSeek = (FilterService)passThroughSeek,
    .fnPeek = (FilterService)passThroughPeek,
    .fnSync = (FilterService)passThroughSync
} ;

/***********************************************************************************************************************************
Create a new buffer filter object.
It allocates an output buffer which matches the block size of the next filter in the pipeline.
***********************************************************************************************************************************/
Filter *bufferFilterNew(Filter *next)
{
    BufferFilter *this = palloc(sizeof(BufferFilter));
    *this = (BufferFilter) {
        .header = (Filter){
            .next = next,
            .iface = &bufferFilterInterface
        }
    };

    return (Filter *)this;
}
