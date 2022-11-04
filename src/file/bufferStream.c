/**
 * Filter which reconciles an input of one block size with output of a different block size.
 * It replicates the functionality of fread/fwrite/fseek, sending and receiving blocks of data.
 */
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include "common/error.h"
#include "common/buffer.h"
#include "common/passThrough.h"

#include "bufferStream.h"

#define palloc malloc

/**
 * Structure containing the state of the stream, including its buffer.
 */
struct BufferStream
{
    Filter filter;                                                  /* Common to all filters */
    Buffer *buf;                                                    /* Local buffer */
    bool readable;
    bool writeable;
};

static const Error errorCantBothReadWrite =
        (Error) {.code=errorCodeFilter, .msg="BufferStream can't read and write the same stream", .causedBy=NULL};

/**
 * Negotiate buffer sizes needed by neighboring filters.
 * Since our primary purpose is to resolve block size differences, we handle
 * single byte blocks (or larger) from upstream, and we match whatever
 * block size is requested downstream.
 */
size_t bufferStreamSize(BufferStream *this, size_t writeSize)
{
    assert(writeSize > 0);
    /* We don't change the size of data when we "transform" it. */
    /*   Note we don't change the data either, so it is an "identity" transform. */
    this->filter.writeSize = writeSize;
    this->filter.readSize = passThroughSize(this, writeSize);

    /* Round the buffer size up to match the basic block sizes. We can be larger, so just pick the bigger of the two. */
    size_t bufSize = sizeRoundUp(16*1024, sizeMax(this->filter.writeSize, this->filter.readSize));
    this->buf = bufferNew(bufSize);

    return 1;
}


/**
 * Open a buffered file, either for reading or writing.
 */
Error bufferStreamOpen(BufferStream *this, char *path, int mode, int perm)
{
    /* We support I/O in only one direction per open. */
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    if (this->readable && this->writeable)
        return errorCantBothReadWrite;

    /* Pass the event to the next filter to actually open the file. */
    return passThroughOpen(this, path, mode, perm);
}


/**
 * Write data to the buffered stream.
 */
size_t bufferStreamWrite(BufferStream *this, Byte *buf, size_t bufSize, Error* error)
{
    if (!errorIsOK(*error))
        return 0;

    /* If our buffer is empty and our write is larger than a single block, avoid the capy and pass it on directly. */
    size_t nextBlockSize = this->filter.next->writeSize;
    if (bufferIsEmpty(this->buf) && bufSize >= nextBlockSize)
        return passThroughWrite(this, buf, bufSize, error);

    /* Copy data into the buffer. */
    size_t actualSize = copyToBuffer(this->buf, buf, bufSize);

    /* Flush the buffer if full, attributing any errors to our write request. */
    bufferFlush(this->buf, this, error);

    return actualSize;
}


/**
 * Read data from the buffered stream.
 *  Note our read request must be at least as large as our successor's block size.
 */
size_t bufferStreamRead(BufferStream *this, Byte *buf, size_t bufSize, Error *error)
{
    if (!errorIsOK(*error))
        return 0;

    /* If our buffer is empty and the read is more than our block size, avoid the copy and read it directly to our caller. */
    if (bufferIsEmpty(this->buf) && bufSize >= this->filter.readSize)
        return passThroughRead(this, buf, bufSize, error);

    /* If buffer is empty, fetch some more data, attributing errors to our read request. */
    bufferFill(this->buf, this, error);

    /* Check for error while filling buffer. */
    if (isError(*error))
        return 0;

    /* Copy data from buffer since all is OK; */
    return copyFromBuffer(this->buf, buf, bufSize);
}


/**
 * Close the buffered stream.
 */
void bufferStreamClose(BufferStream *this, Error *error)
{
    /* Flush our buffers. */
    bufferForceFlush(this->buf, this, error);

    /* Pass on the close request., */
    passThroughClose(this, error);

    this->readable = this->writeable = false;
}


/**
 * Synchronize any written data to persistent storage.
 */
void bufferStreamSync(BufferStream *this, Error *error)
{
    /* Flush our buffers. */
    bufferForceFlush(this->buf, this, error);

    /* Pass on the sync request */
    passThroughSync(this, error);
}

FilterInterface bufferStreamInterface = (FilterInterface)
{
    .fnOpen = (FilterOpen)bufferStreamOpen,
    .fnWrite = (FilterWrite)bufferStreamWrite,
    .fnClose = (FilterClose)bufferStreamClose,
    .fnRead = (FilterRead)bufferStreamRead,
    .fnSync = (FilterSync)bufferStreamSync,
    .fnSize = (FilterSize)bufferStreamSize,
} ;

/***********************************************************************************************************************************
Create a new buffer filter object.
It allocates an output buffer which matches the block size of the next filter in the pipeline.
***********************************************************************************************************************************/
Filter *bufferStreamNew(Filter *next)
{
    BufferStream *this = palloc(sizeof(BufferStream));

    return filterInit(this, &bufferStreamInterface, next);
}
