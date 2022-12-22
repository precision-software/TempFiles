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
 *       a) If the buffer is partial or dirty, the actual file position matches blockPosition.
 *       b) If the buffer is full of clean data, the actual file position matches blockPosition+cipherSize.
 *
 *  One goal is to ensure purely sequential reads/writes do not require Seek operations.
 *
 * TODO: Open should return a new instance each time, so we can open multiple files at once.
 */
#include <stdlib.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <assert.h>
#include "common/error.h"
#include "common/passThrough.h"
#include "common/debug.h"

#include "file/buffered.h"

#define palloc malloc

/**
 * Structure containing the state of the stream, including its buffer.
 */
struct Blockify
{
    Filter filter;        /* Common to all filters */
    size_t suggestedSize; /* The suggested buffer size. We may make it a bit bigger */

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
size_t blockifySeek(Blockify *this, size_t position, Error *error);
static size_t copyOut(Blockify *this, Byte *buf, size_t size);
static size_t copyIn(Blockify *this, Byte *buf, size_t size);
static bool flushBuffer(Blockify *this, Error *error);
static bool fillBuffer(Blockify *this, Error *error);
size_t directWrite(Blockify *this, Byte *buf, size_t size, Error *error);
size_t directRead(Blockify *this, Byte *buf, size_t size, Error *error);

/**
 * Open a buffered file, reading, writing or both.
 */
Blockify *blockifyOpen(Blockify *pipe, char *path, int oflags, int perm, Error *error)
{
    /* Below us, we need to read/modify/write even if write only. */
    if ( (oflags & O_ACCMODE) == O_WRONLY)
        oflags = (oflags & ~O_ACCMODE) | O_RDWR;

    /* Open the downstream file and clone ourselves */
    Filter *next = passThroughOpen(pipe, path, oflags, perm, error);
    Blockify *this = blockifyNew(pipe->suggestedSize, next);
    if (isError(*error))
        return this;

    /* Are we read/writing or both? */
    this->readable = (oflags & O_ACCMODE) != O_WRONLY;
    this->writeable = (oflags & O_ACCMODE) != O_RDONLY;

    /* Position to the start of file */
    this->position = 0;
    this->blockPosition = 0;

    /* Start with an empty buffer */
    this->dirty = false;
    this->blockActual = 0;

    /* We don't know the size of the file yet. */
    this->fileSize = 0;
    this->sizeConfirmed = (oflags & O_TRUNC) == O_TRUNC;

    /* We don't know record size yet, so we will allocate buffer in the Size event */
    this->buf = NULL;

    return this;
}


/**
 * Write data to the buffered file.
 */
size_t blockifyWrite(Blockify *this, Byte *buf, size_t size, Error* error)
{
    debug("blockifyWrite: size=%zu  position=%zu \n", size, this->position);
    assert(size > 0);
    if (isError(*error))
        return 0;

    /* If we are at end of current buffer. */
    if (this->position == this->blockPosition + this->blockSize)
    {
        /* Clean the buffer if dirty */
        flushBuffer(this, error);

        /* Move to the next block (with empty buffer) */
        this->blockPosition += this->blockSize;
        this->blockActual = 0;
    }

    /* If buffer is empty, position is aligned, and the data exceeds block size, write direct to next stage */
    if (this->buf == NULL || this->blockActual == 0 && this->position == this->blockPosition && size >= this->blockSize)
        return directWrite(this, buf, size, error);

    /* If buffer is empty ... */
    if (this->blockActual == 0 && this->readable)
    {
        /* Fill the buffer, ignoring EOF */
        fillBuffer(this, error);
        if (errorIsEOF(*error))
            *error = errorOK;
    }

    /* If we are dirtying a clean buffer, then seek backwards to the start of buffer */
    if (!this->dirty)
        passThroughSeek(this, this->blockPosition, error);

    /* Copy data in and update position */
    size_t actual = copyIn(this, buf, size);
    this->dirty = true;
    this->position += actual;
    this->dirty = true;

    assert(actual > 0);
    return actual;
}


size_t directWrite(Blockify *this, Byte *buf, size_t size, Error *error)
{
    /* Write out multiple records, but no partials */
    size_t alignedSize = sizeRoundDown(size, this->blockSize);
    size_t actual = passThroughWriteAll(this, buf, alignedSize, error);

    /* Update positions */
    this->position += actual;
    this->blockPosition = sizeRoundDown(this->position, this->blockSize);

    return actual;
}

/**
 * Read bytes from the buffered stream.
 * Note it may take multiple reads to get all the data or to reach EOF.
 */
size_t blockifyRead(Blockify *this, Byte *buf, size_t size, Error *error)
{
    debug("blockifyRead: position=%zu size=%zu cipherSize=%zu\n", this->position, size, this->blockSize);
    if (!errorIsOK(*error))
        return 0;

    /* If we are at the end of the current (non-empty) buffer */
    if (this->position == this->blockPosition + this->blockActual && this->blockActual > 0)
    {
        /* If the buffer is partial, then we are EOF */
        if (this->blockActual < this->blockSize)
            return setError(error, errorEOF);

        /* Clean the buffer if dirty */
        flushBuffer(this, error);

        /* Advance to the next buffer position, with an empty buffer */
        this->blockPosition += this->blockSize;
        this->blockActual = 0;
    }

    /* Optimization. See if we skip our buffer and talk directly to the next stage */
    if (this->buf == NULL || this->position == this->blockPosition && size > this->blockSize && this->blockActual == 0)
        return directRead(this, buf, size, error);

        /* If our buffer is empty fill it in.  Exit on error or EOF */
    if (this->blockActual == 0 && fillBuffer(this, error))
        return 0;

    /* Copy bytes out from our internal buffer. */
    size_t actual = copyOut(this, buf, size);
    this->position += actual;

    /* Return the number of bytes transferred. */
    debug("blockifyRead: actual=%zu\n", actual);
    return actual;
}


size_t directRead(Blockify *this, Byte *buf, size_t size, Error *error)
{
    debug("directRead: size=%zu  position=%zu cipherSize=%zu\n", size, this->position, this->blockSize);
    /* Read multiple records, but no partials */
    size_t alignedSize = sizeRoundDown(size, this->blockSize);
    size_t actual = passThroughReadAll(this, buf, alignedSize, error);

    /* If we read a partial block, claw it back from the caller's buffer */
    size_t actualPartial = actual % this->blockSize;
    size_t actualBlock = actual - actualPartial;
    if (actualPartial > 0)
        copyIn(this, buf+actualBlock, actualPartial);

    /* Update positions */
    this->position += actualBlock;
    this->blockPosition += actualBlock;
    if (actualPartial > 0)
        this->fileSize = this->position + actual;

    debug("directRead: actual=0x%zu\n", actualBlock);
    return actualBlock;
}


/**
 * Seek to a position
 */
size_t blockifySeek(Blockify *this, size_t position, Error *error)
{
    debug("blockifySeek: this->position=%zu  position=%lld\n", this->position, (off_t)position);
    if (isError(*error))
        return this->position;

    /* If seeking to end, ... */
    if (position == FILE_END_POSITION)
    {
        /* Clean our buffer if needed. TODO: KLUDGE get file size without losing the dirty data */
        flushBuffer(this, error);
        this->blockPosition = FILE_END_POSITION;  /* An invalid position so we will always seek */

        /* Get the actual file size, and position ourselves at end of last full block */
        this->fileSize = passThroughSeek(this, FILE_END_POSITION, error);
        position = this->fileSize;
    }

    /* If we are moving to a different block ... */
    size_t newBlock = sizeRoundDown(position, this->blockSize);
    debug("blockifySeek: position=%zu  newBlock=%zu blockPosition=%zu\n", position, newBlock, this->blockPosition);
    if (newBlock != this->blockPosition)
    {
        /* If dirty, flush current block. */
        flushBuffer(this, error);

        /* Position to new block in file. */
        passThroughSeek(this, newBlock, error);
        this->blockPosition = newBlock;
        this->blockActual = 0;
    }

    /* Update position */
    this->position = position;

    return position;
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
    free(this);

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
 * single byte blocks (or larger) from upstream, and we match a multiple of
 * whatever block size is requested downstream.
 */
size_t blockifyBlockSize(Blockify *this, size_t prevSize, Error *error)
{
    /* Suggest a block size bigger than what is requested of us. */
    size_t suggestedSize = sizeMax(this->suggestedSize, prevSize);

    /* We have a suggested block size. Pass that on. */
    size_t requestedSize = passThroughBlockSize(this, suggestedSize, error);

    /* Our actual size will be a multiple of the requested size */
    this->blockSize = sizeRoundUp(suggestedSize, requestedSize);

    /* Resize the buffer we created during Open() . */
    flushBuffer(this, error);
    this->buf = realloc(this->buf, this->blockSize);
    this->blockActual = 0;

    /* We are buffering, so tell the caller we can accept any size. */
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


/**
 Create a new buffer filter object.
 It converts input bytes to records expected by the next filter in the pipeline.
 */
Blockify *blockifyNew(size_t suggestedSize, void *next)
{
    Blockify *this = palloc(sizeof(Blockify));
    *this = (Blockify){0};

    /* Set the suggested buffersize, defaulting to 16Kb */
    if (suggestedSize == 0)
        this->suggestedSize = 16*1024;
    else
        this->suggestedSize = suggestedSize;

    return filterInit(this, &blockifyInterface, next);
}



/*
 * Clean a dirty buffer by writing it to disk. Does not change the contents of the buffer.
 */
static bool flushBuffer(Blockify *this, Error *error)
{
    debug("flushBuffer: position=%zu  bufActual=%zu  dirty=%d\n", this->position, this->blockActual, this->dirty);

    /* if the buffer is dirty, flush it. We reestablish assertion 3a */
    if (this->dirty && this->blockActual > 0)
        passThroughWriteAll(this, this->buf, this->blockActual, error);
    this->dirty = false;

    /* Update file size */
    this->fileSize = sizeMax(this->fileSize, this->blockPosition + this->blockActual);

    return isError(*error);
}

/*
 * Read in a new buffer of data for the current position
 */
static bool fillBuffer (Blockify *this, Error *error)
{
    assert(!this->dirty);
    debug("fillBuffer: bufActual=%zu  blockPosition=%zu sizeConfirmed=%d  fileSize=%zu\n",
          this->blockActual, this->blockPosition, this->sizeConfirmed, this->fileSize);

    /* Quick check for EOF (without system calls) */
    /* TODO */

    /* Read in the current buffer */
    this->blockActual = passThroughReadAll(this, this->buf, this->blockSize, error);

    /* if EOF or partial read, update the known file size */
    /* TODO: */

    return isError(*error);
}

static size_t copyIn(Blockify *this, Byte *buf, size_t size)
{
    /* Copy bytes into the buffer, up to end of data or end of buffer */
    size_t offset = this->position - this->blockPosition;
    size_t actual = sizeMin(this->blockSize-offset, size);
    memcpy(this->buf + offset, buf, actual);
    debug("copyIn: size=%zu blockPosition=%zu bufActual=%zu offset=%zu  actual=%zu\n",
          size, this->blockPosition, this->blockActual, offset, actual);

    /* We may have extended the total data held in the buffer */
    if (actual + offset > this->blockActual)
        this->blockActual = actual + offset;

    assert(this->blockActual <= this->blockSize);
    return actual;
}

static size_t copyOut(Blockify *this, Byte *buf, size_t size)
{
    size_t offset = this->position - this->blockPosition;
    size_t actual = sizeMin(this->blockActual-offset, size);
    memcpy(buf, this->buf + offset, actual);
    debug("copyOut: size=%zu blockPosition=%zu bufActual=%zu offset=%zu  actual=%zu\n",
          size, this->blockPosition, this->blockActual, offset, actual);
    return actual;
}
