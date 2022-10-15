/***********************************************************************************************************************************

***********************************************************************************************************************************/
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include "common/error.h"
#include "common/buffer.h"
#include "common/passThrough.h"

#include "bufferFilter.h"

#define palloc malloc

/***********************************************************************************************************************************
Filter which reconciles an input of one block size with output of a different block size.
It replicates the functionality of fread/fwrite/fseek, sending and receiving blocks of data.
***********************************************************************************************************************************/
struct BufferFilter
{
    Filter header;                                                  /* Common to all filters */
    Buffer *buf;                                                    /* Local buffer */
    bool readable;
    bool writeable;
};

Error errorCantBothReadWrite =
        (Error) {.code=errorCodeFilter, .msg="BufferFilter can't read and write the same stream", .causedBy=NULL};

Error
bufferFilterOpen(BufferFilter *this, char *path, int mode, int perm)
{
    // We support I/O in only one direction per open.
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    if (this->readable && this->writeable)
        return errorCantBothReadWrite;

    // Pass the open request to the next filter and get the response.
    return passThroughOpen(this, path, mode, perm);
}


size_t bufferFilterWrite(BufferFilter *this, Byte *buf, size_t bufSize, Error* error)
{
    if (!errorIsOK(*error))
        return 0;

    // If our buffer is empty and our write is larger than a single block, avoid the capy and pass it on directly.
    size_t nextBlockSize = this->header.next->blockSize;
    if (bufferIsEmpty(this->buf) && bufSize >= nextBlockSize)
        return passThroughWrite(this, buf, bufSize, error);

    // Copy data into the buffer.
    size_t actualSize = writeToBuffer(this->buf, buf, bufSize);

    // Flush the buffer if full, attributing any errors to our write request.
    *error = bufferFlush(this->buf, this);

    return actualSize;
}

size_t bufferFilterRead(BufferFilter *this, Byte *buf, size_t bufSize, Error *error)
{
    if (!errorIsOK(*error))
        return 0;

    // If our buffer is empty and our read is more our successor's block size, avoid the copy and pass it on directly.
    size_t nextBlockSize = this->header.next->blockSize;
    if (bufferIsEmpty(this->buf) && bufSize >= nextBlockSize)
        return passThroughRead(this, buf, bufSize, error);

    // If buffer is empty, fetch some more data, attributing errors to our read request.
    *error = bufferFill(this->buf, this);

    // Check for error while filling buffer.
    if (!errorIsOK(*error))
        return 0;

    // Copy data from buffer since all is OK;
    return readFromBuffer(this->buf, buf, bufSize);
}


void bufferFilterClose(BufferFilter *this, Error *error)
{
    // Flush our buffers.
    Error flushError = bufferForceFlush(this->buf, this);

    // Pass on the close request
    passThroughClose(this, &flushError);

    // but give priority to any previous errors.
    if (errorIsOK(*error))
        *error = flushError;
}

FilterInterface bufferFilterInterface = (FilterInterface)
{
    .fnOpen = (FilterOpen)bufferFilterOpen,
    .fnWrite = (FilterWrite)bufferFilterWrite,
    .fnClose = (FilterClose)bufferFilterClose,
    .fnRead = (FilterRead)bufferFilterRead,
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
            .blockSize = 1,
            .iface = &bufferFilterInterface
        },

        // Allocate a buffer to hold multiple blocks of our successor.
        .buf = bufferNew(sizeRoundUp(16 * 1024, next->blockSize))
    };

    return (Filter *)this;
}
