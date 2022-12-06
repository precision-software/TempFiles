/**
 * Blockify reconciles a byte stream input with an output of fixed size blocks.
 * Because output blocks are fixed size, it is possible to do random Seeks
 * and Writes to the output file.
 *
 * Blockify replicates the functionality of fread/fwrite/fseek.
 * Seeks and O_APPEND are not compatible with subsequent streaming filters which create
 * variable size blocks. (eg. compression).
 *
 * Note that Blockify could support O_DIRECT files if the last block is padded and any
 * metadata is stored in a separate location. This is a future goal.
 *
 * Some logical assertions about blocks and file position.
 *    1) All I/Os to actual file are block aligned.  ("actual file" means next stage in pipeline.)
 *    2) All I/Os transfer an entire block, except for the final block in the file.
 *    3) The actual file is pre-positioned for the next I/O.
 *       a) If the buffer is empty or dirty, the actual file position matches blockPosition.
 *       b) If the buffer has clean data, the actual file position matches blockPosition+blockSize.
 *    Purely sequential reads/writes do not require Seek operations.
 *    If dirty, our internal buffer is not empty.
 *
 * TODO: Open should return a new instance each time, so we can open multiple files at once.
 */
#include <stdlib.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <assert.h>
#include "common/error.h"
#include "common/passThrough.h"

#include "buffered.h"

#define palloc malloc

/**
 * Structure containing the state of the stream, including its buffer.
 */
struct Blockify
{
    Filter filter;        /* Common to all filters */

    size_t blockSize;     /* The size of blocks we read/write to our successor. */
    Byte *buf;            /* Local buffer, precisely one block in size. */
    bool dirty;           /* Does the buffer contain dirty data? */

    size_t position;      /* Current byte position in the file. */
    size_t blockPosition; /* Byte position of the beginning of the buffer */
    size_t blockActual;   /* Nr of actual bytes in the buffer */

    size_t fileSize;      /* Highest byte position we've seen so far for the file. */
    bool sizeConfirmed;   /* fileSize is confirmed as actual file size. */

    bool readable;        /* Opened for reading */
    bool writeable;       /* Opened for writing */
};


/* Forward references */
void writeBlock(Blockify *this, size_t position, Error *error);
void readBlock(Blockify *this, size_t position, Error *error);
void flushCurrentBlock(Blockify *this, Error *error);
void fillCurrentBlock(Blockify *this, Error *error);
size_t blockifySeek(Blockify *this, size_t position, Error *error);

/**
 * Open a buffered file, reading, writing or both.
 */
Error blockifyOpen(Blockify *this, char *path, int mode, int perm)
{
    /* Are we read/writing or both? */
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;

    /* Position to the start of file */
    this->position = 0;
    this->blockPosition = 0;

    /* Start with an empty buffer */
    this->dirty = false;
    this->blockActual = 0;

    /* We don't know the size of the file yet. */
    this->fileSize = 0;
    this->sizeConfirmed = (mode & O_TRUNC) == O_TRUNC;

    /* Pass the open event to the next filter to actually open the file. */
    Error error = passThroughOpen(this, path, mode, perm);

    return error;
}


/*
 * A debug function to verify the various buffer pointers are consistent with each other.
 */
static void checkBuffer(Blockify(*this))
{
    assert(this->fileSize >= this->blockPosition + this->blockActual);
    assert(this->blockPosition <= this->position);
    assert(this->position <= this->fileSize);
    assert(this->blockPosition + this->blockActual >= this->position || this->blockActual == 0);
    size_t offset = this->position % this->blockSize;
    assert(this->blockPosition+offset == this->position || this->blockPosition+this->blockSize == this->position);
    assert(this->blockActual <= this->blockSize);
}



static bool flushBuffer(Blockify *this, Error *error)
{
    /* Check for holes. */
    if (this->position > this->fileSize)
        return filterError(error, "Positioned beyond End-Of-File - holes not allowed.");

    /* if the block is dirty, flush it. */
    if (this->dirty && this->blockActual > 0)
        passThroughWriteAll(this, this->buf, this->blockActual, error);
    this->dirty = false;

    return isError(*error);
}

/*
 * Advance to the next buffer if we are at the end of the current buffer.
 */
static bool nextBuffer(Blockify *this, Error *error)
{
    /* If we are positioned at the end of the current buffer, ... */
    if (this->blockActual > 0 && this->position >= this->blockPosition + this->blockSize)
    {
        /* Write out the current buffer if dirty */
        if (flushBuffer(this, error))
            return true;

        /* Position to the beginning of the next buffer */
        this->blockPosition += this->blockSize;
        this->blockActual = 0;

        if (this->blockPosition > this->fileSize)
            this->fileSize = this->position;
    }

    return isError(*error);
}


static bool fillBuffer (Blockify *this, Error *error)
{
    if (this->blockActual == 0)
    {
        this->blockActual = passThroughRead(this, this->buf, this->blockSize, error);
        this->sizeConfirmed |= this->blockActual < this->blockSize;
        if (this->blockPosition + this->blockActual > this->fileSize)
            this->fileSize = this->blockPosition + this->blockActual;
    }

    return isError(*error);
}

static size_t copyIn(Blockify *this, Byte *buf, size_t size)
{
    size_t offset = this->position - this->blockPosition;
    size_t actual = sizeMin(this->blockSize-offset, size);
    memcpy(this->buf + offset, buf, actual);
    return actual;
}

static size_t copyOut(Blockify *this, Byte *buf, size_t size)
{
    size_t offset = this->position - this->blockPosition;
    size_t actual = sizeMin(this->blockActual-offset, size);
    memcpy(buf, this->buf + offset, actual);
    return actual;
}


/**
 * Write data to the buffered file.
 */
size_t blockifyWrite(Blockify *this, Byte *buf, size_t size, Error* error)
{
    if (isError(*error))
        return 0;

    /* Advance to next buffer if appropriate. */
    nextBuffer(this, error);

    /* If our buffer is empty and the request is aligned and big enough, ... */
    if (this->blockActual == 0 && this->position == this->blockPosition && size >= this->blockSize)
    {
        /* Bypass our buffer and write a block directly to the next stage */
        size_t actual = passThroughWriteAll(this, buf, this->blockSize, error);
        if (isError(*error))
            return 0;

        /* Update our position to reflect the write */
        this->position += actual;
        this->blockPosition += actual;

        if (this->position > this->fileSize)
            this->fileSize = this->position;
        return actual;
    }

    /* If we are both read+writing, read before modifying. */
    /*   Note we are moving from assertion 3a to assertion 3b. */
    if (this->readable) // TODO: use known file size as well.
        fillBuffer(this, error);

    /* if we are about to dirty a clean buffer, then seek back to beginning of buffer */
    /*  Note we are moving from assertion 3b to assertion 3a. */
    if (this->blockActual > 0 && !this->dirty)
        passThroughSeek(this, this->blockPosition, error);
    this->dirty = true;

    /* Copy data into the buffer */
    size_t actual = copyIn(this, buf, size);
    this->position += actual;

    /* We may have extended valid data at the end of block. */
    size_t offset = this->position - this->blockPosition;
    if (offset > this->blockActual)
        this->blockActual = offset;

    /* Update our knowledge about the size of the file. */
    if (this->blockPosition + this->blockActual > this->fileSize)
        this->fileSize = this->blockPosition + this->blockActual;

    /* done */
    return actual;
}


/**
 * Read bytes from the buffered stream.
 * Note it may take multiple reads to get all the data or to reach EOF.
 */
size_t blockifyRead(Blockify *this, Byte *buf, size_t size, Error *error)
{
    if (!errorIsOK(*error))
        return 0;

    /* Advance to next buffer if appropriate. */
    nextBuffer(this, error);
    if (isError(*error))
        return 0;

    /* If our buffer is empty and the request is big enough and aligned, ... */
    if (this->blockActual == 0 && size >= this->blockSize && this->position == this->blockPosition)
    {
        /* Bypass our buffer and read a block directly from the next stage */
        size_t actual = passThroughRead(this, buf, this->blockSize, error);
        if (isError(*error))
            return 0;

        /* Update our position to reflect the read. TODO: what about partial read? */
        if (actual == this->blockSize)
            this->blockPosition += this->blockSize;
        this->position += actual;

        /* Track the file size as well as we can. */
        this->sizeConfirmed |= actual < this->blockSize;
        if (this->position > this->fileSize)
            this->fileSize = this->position;

        return actual;
    }

    /* Read in the current buffer if not already done. */
    if (this->blockActual == 0)
    {
        fillBuffer(this, error);
        if (isError(*error))
            return 0;
    }

    /* If we are still positioned at end of buffer, then EOF.  (explain?) */
    if (this->position == this->blockPosition + this->blockActual)
        return setError(error, errorEOF);

    /* If we are positioned beyond end of buffer, then HOLE. (explain?) */
    if (this->position > this->blockPosition + this->blockActual)
        return filterError(error, "Attempting to read past End-Of-File.");

    /* Copy bytes out from our internal buffer. */
    size_t actual = copyOut(this, buf, size);
    this->position += actual;

    /* If we reached end of block, then prepare to read the next block */
    assert(this->position <= this->blockPosition + this->blockSize);
    if (this->position == this->blockPosition + this->blockSize)
    {
        this->blockPosition = this->position;
        this->blockActual = 0;
    }

    /* Return the number of bytes transferred. */
    return actual;
}


/**
 * Seek to a position
 */
size_t blockifySeek(Blockify *this, size_t position, Error *error)
{
    if (isError(*error))
        return this->position;

    /* If we are moving to a different block ... */
    size_t newBlock = position - position % this->blockSize;
    if (newBlock != this->blockPosition)
    {
        /* If dirty, flush current block. */
        flushBuffer(this, error);

        /* Position to new block in file. */
        size_t actualPosition = passThroughSeek(this, newBlock, error);
        this->blockPosition = newBlock;
        this->blockActual = 0;
    }

    /* Check for positioning beyond end of file */
    /* TODO: */

    /* At this point, we are pointing to the desired block. Update position */
    this->position = position;

    /* Check for seek beyond filesize */
    if (this->position > this->fileSize)
        this->fileSize = this->position; // TODO: What about holes?

    return this->position;
}

/**
 * Close the buffered file.
 */
void blockifyClose(Blockify *this, Error *error)
{
    /* Flush our buffers. */
    flushBuffer(this, error);

    /* Pass on the close request., */
    passThroughClose(this, error);

    this->readable = this->writeable = false;
    if (this->buf != NULL)
        free(this->buf);
}


/**
 * Synchronize any written data to persistent storage.
 */
void blockifySync(Blockify *this, Error *error)
{
    /* Flush our buffers. */
    flushBuffer(this, error);

    /* Pass on the sync request */
    passThroughSync(this, error);
}



/**
 * Negotiate buffer sizes needed by neighboring filters.
 * Since our primary purpose is to resolve block size differences, we handle
 * single byte blocks (or larger) from upstream, and we match whatever
 * block size is requested downstream.
 *
 * Question: should we throw error if our requested blocksize doesn't
 * match downstream block size?  TODO: Work this out, as 1 may be a common block size even when we want buffering.
 */
size_t blockifyBlockSize(Blockify *this, size_t prevSize, Error *error)
{
    /* Suggest a block size as 16K rounded up to a multiple of the previous block size. */
    size_t suggestSize = sizeRoundUp(16*1024, prevSize);

    /* We have a suggested block size. Pass that on, but accept whatever we get back */
    this->blockSize = passThroughBlockSize(this, suggestSize, error);

    /* Allocate a buffer. */
    this->buf = palloc(this->blockSize);

    /* Tell the caller we can accept any size. */
    return 1;
}


FilterInterface blockifyInterface = (FilterInterface)
        {
                .fnOpen = (FilterOpen)blockifyOpen,
                .fnWrite = (FilterWrite)blockifyWrite,
                .fnClose = (FilterClose)blockifyClose,
                .fnRead = (FilterRead)blockifyRead,
                .fnSync = (FilterSync)blockifySync,
                .fnBlockSize = (FilterBlockSize)blockifyBlockSize,
                .fnSeek = (FilterSeek)blockifySeek,
        } ;

/***********************************************************************************************************************************
Create a new buffer filter object.
It allocates an output buffer which matches the block size of the next filter in the pipeline.
***********************************************************************************************************************************/
Filter *blockifyNew(size_t blockSize, Filter *next)
{
    Blockify *this = palloc(sizeof(Blockify));
    this->blockSize = blockSize;

    return filterInit(this, &blockifyInterface, next);
}
