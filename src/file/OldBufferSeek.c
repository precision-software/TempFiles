/**
 * BufferSeek reconciles a byte stream input with an output of fixed size blocks.
 * Because output blocks are fixed size, it is possible to do random Seeks
 * and Writes to the output file.
 *
 * BufferSeek replicates the functionality of fread/fwrite/fseek.
 * It is not compatible with subsequent streaming filters which change the size
 * of the output blocks.
 *  - If the subsequent filters generate variable size blocks (eg. compression)
 *    there is no easy way to calculate an offset to seek to.
 *  - If the subsequent filters have fixed but unaligned blocks, then
 *    a single random write can translate into 4 physical disk I/Os.
 *
 * Note that BufferSeek can support O_DIRECT files if desired.
 *
 * Some logical assertions:
 *    All I/O to next stage is block aligned.
 *    All reads and writes transfer an entire block, except for the final block in the file.
 *    this->position * this->blkSize + bufferReadSize(this->buf)  points to our current file position.
 *    If dirty, our internal buffer is not empty.
 *    Sequential reads/writes do not require Seek operations.
 */
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include "common/error.h"
#include "common/buffer.h"
#include "common/passThrough.h"

#include "bufferSeek.h"

#define palloc malloc

/**
 * Structure containing the state of the stream, including its buffer.
 */
struct BufferSeek
{
    Filter filter;        /* Common to all filters */
    Buffer *buf;          /* Local buffer, precisely one block in size. */
    bool dirty;           /* Does the buffer contain dirty data? */
    size_t blockIdx;      /* Index position of the current block in the buffer. */
    size_t fileSize;      /* Highest block index we've read or written so far */
    bool readable;
    bool writeable;
    size_t blockSize;
};

static const Error errorCantBothReadWrite =
        (Error) {.code=errorCodeFilter, .msg="BufferSeek can't read and write the same stream", .causedBy=NULL};

/* Forward references */
void writeBlock(BufferSeek *this, size_t position, Error *error);
void readBlock(BufferSeek *this, size_t position, Error *error);
void flushCurrentBlock(BufferSeek *this, Error *error);
void fillCurrentBlock(BufferSeek *this, Error *error);

/**
 * Negotiate buffer sizes needed by neighboring filters.
 * Since our primary purpose is to resolve block size differences, we handle
 * single byte blocks (or larger) from upstream, and we match whatever
 * block size is requested downstream.
 *
 * Question: should we throw error if our requested blocksize doesn't
 * match downstream block size?
 */
size_t bufferSeekSize(BufferSeek *this, size_t writeSize)
{
    assert(writeSize > 0);
    /* We don't change the size of data as it passes through, although we may set a larger block size. */
    this->filter.writeSize = sizeMax(this->blockSize, writeSize);
    this->filter.readSize = sizeMax(this->blockSize, passThroughSize(this, writeSize));

    /* Round the buffer size up to match the basic block sizes. We can be larger, so just pick the bigger of the two. */
    size_t bufSize = sizeMax(this->filter.writeSize, this->filter.readSize);
    this->buf = bufferNew(bufSize);

    return 1;
}


/**
 * Open a buffered file, either for reading or writing.
 */
Error bufferSeekOpen(BufferSeek *this, char *path, int mode, int perm)
{
    /* We support I/O in only one direction per open. */
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    this->dirty = false;
    this->blockIdx = 0;
    bufferReset(this->buf);

    /* Pass the event to the next filter to actually open the file. */
    return passThroughOpen(this, path, mode, perm);
}


/**
 * Write data to the buffered stream.
 */
size_t bufferSeekWrite(BufferSeek *this, Byte *buf, size_t bufSize, Error* error)
{
    if (!errorIsOK(*error))
        return 0;

    /* For large aligned writes, don't do read/modify/write */
    if (bufferIsEmpty(this->buf) && bufSize >= this->blockSize)
    {
        /* Write 1 or more full blocks to the next stage. */
        size_t actualBlocks = bufSize / this->blockSize;
        size_t actual = actualBlocks * this->blockSize;
        passThroughWriteAll(this, buf, actual, error);
        if (isError(*error))
            return 0;

        /* Update the current file position, and done. */
        this->blockIdx += actualBlocks;
        return actual;
    }

    /* Fill the buffer if read/modify/write  */
    if (bufferIsEmpty(this->buf) && this->blockIdx < this->fileSize)
        readBlock(this, this->blockIdx, error);

    /* Copy data into current position in the buffer. */
    size_t actualSize = copyIntoBuffer(this->buf, buf, bufSize);
    this->dirty = true;

    /* Flush the buffer if full, attributing any errors to our write request. */
    if (bufferIsFull(this->buf))
    {
        writeBlock(this, this->blockIdx, error);
        if (isError(*error))
            return 0;
    }

    return actualSize;
}


/**
 * Read data from the buffered stream.
 *  Note our read request must be at least as large as our successor's block size.
 */
size_t bufferSeekRead(BufferSeek *this, Byte *buf, size_t bufSize, Error *error)
{
    if (!errorIsOK(*error))
        return 0;

    /* For large aligned reads, do the read directly. */
    if (bufferIsEmpty(this->buf) && bufSize >= this->blockSize)
    {
        /* Read 1 or more full blocks from the next stage. */
        size_t nrBlocks = bufSize / this->blockSize;
        size_t size = nrBlocks * this->blockSize;
        size_t actual = passThroughReadAll(this, buf, size, error);
        if (isError(*error))
            return 0;

        /* Update the current file position, and done. */
        size_t actualBlocks = (actual + this->blockSize -1) / this->blockSize;
        this->blockIdx += actualBlocks;
        return actual;
    }

    /* If buffer is empty, fetch a block of data. */
    if (bufferIsEmpty(this->buf))
        readBlock(this, this->blockIdx, error);

    /* Check for error while filling buffer. */
    if (isError(*error))
        return 0;

    /* Copy data from buffer since all is OK; */
    size_t size = copyFromBuffer(this->buf, buf, bufSize);

    return size;
}


/**
 * Seek to a position
 */
void bufferSeekSeek(BufferSeek *this, size_t position, Error *error)
{
    if (isError(*error))
        return;

    /* Get the block index for that position */
    size_t newBlock = position / this->blockSize;

    /* If we are moving to a different block ... */
    if (newBlock != this->blockIdx)
    {
        /* If dirty, flush current block. */
        flushCurrentBlock(this, error);

        /* Make note of new current block. Note we haven't seeked yet. */
        this->blockIdx = newBlock;
    }

    /* If we are positioned to the middle of a block, fetch the block and update the buffer pointer */
    size_t offset = position % this->blockSize;
    if (offset != 0)
        fillCurrentBlock(this, error);
    this->buf->current = this->buf->beginBuf + offset;
}

/**
 * Close the buffered file.
 */
void bufferSeekClose(BufferSeek *this, Error *error)
{
    /* Flush our buffers. */
    flushCurrentBlock(this, error);

    /* Pass on the close request., */
    passThroughClose(this, error);

    this->readable = this->writeable = false;
}


/**
 * Synchronize any written data to persistent storage.
 */
void bufferSeekSync(BufferSeek *this, Error *error)
{
    /* Flush our buffers. */
    bufferForceFlush(this->buf, this, error);

    /* Pass on the sync request */
    passThroughSync(this, error);
}

FilterInterface bufferSeekInterface = (FilterInterface)
        {
                .fnOpen = (FilterOpen)bufferSeekOpen,
                .fnWrite = (FilterWrite)bufferSeekWrite,
                .fnClose = (FilterClose)bufferSeekClose,
                .fnRead = (FilterRead)bufferSeekRead,
                .fnSync = (FilterSync)bufferSeekSync,
                .fnSize = (FilterSize)bufferSeekSize,
                .fnSeek = (FilterSeek)bufferSeekSeek,
        } ;

/***********************************************************************************************************************************
Create a new buffer filter object.
It allocates an output buffer which matches the block size of the next filter in the pipeline.
***********************************************************************************************************************************/
Filter *bufferSeekNew(size_t blockSize, Filter *next)
{
    BufferSeek *this = palloc(sizeof(BufferSeek));
    this->blockSize = blockSize;

    return filterInit(this, &bufferSeekInterface, next);
}


void readBlock(BufferSeek *this, size_t blockIdx, Error *error)
{
    /* If we already read the current block, then our current position is one bigger. */
    /*  In the case of a partial, final block, we'll get an EOF in the next read. */
    if (this->buf->beginBuf != this->buf->endData)
        this->blockIdx++;  // TODO: Want errors to be noop.

    /* Issue a Seek if we aren't already there. */
    if (blockIdx != this->blockIdx)
    {
        passThroughSeek(this, blockIdx * this->blockSize, error);
        this->blockIdx = blockIdx;  // TODO: Error handling.
        bufferReset(this->buf); // TODO: is this needed?
    }

    /* Read the block from the next stage */
    size_t size = passThroughReadAll(this, this->buf->beginBuf, this->blockSize, error);
    if (isError(*error))
        return;

    /* Update buffer to reflect the data we just read in */
    this->buf->endData = this->buf->beginBuf + size;
    this->buf->current = this->buf->beginBuf;

    /* Update current block index and extend file size if we discovered new size. */
    this->blockIdx = blockIdx;
    this->fileSize = sizeMax(this->fileSize, this->blockIdx+1);
}


void writeBlock(BufferSeek *this, size_t blockIdx, Error *error)
{
    /* Seek if we are already there. */
    if (blockIdx != this->blockIdx + 1)
        passThroughSeek(this, this->blockIdx * this->blockSize, error);
    this->blockIdx = blockIdx;

    /* Write the block, extending file size if it grew. */
    passThroughWriteAll(this, this->buf->beginBuf, bufferDataSize(this->buf), error);
    this->fileSize = sizeMax(this->fileSize, this->blockIdx+1);

    /* Clear out the buffer */
    this->dirty = false;
}

void fillCurrentBlock(BufferSeek *this, Error *error)
{
    //if (bufferIsEmpty(this->buf) && errorIsOK(*error))
    if (this->buf->beginBuf == this->buf->endData && errorIsOK(*error))
        readBlock(this, this->blockIdx, error);
}

void flushCurrentBlock(BufferSeek *this, Error *error)
{
    if (this->dirty)
        writeBlock(this, this->blockIdx, error);

    //bufferReset(this->buf);
    this->dirty = false;
}
