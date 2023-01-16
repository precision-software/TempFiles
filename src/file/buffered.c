/**
 * Buffered reconciles a byte stream input with an output of fixed size blocks.
 * Because output blocks are fixed size, it is possible to do random Seeks
 * and Writes to the output file.
 *
 * Buffered replicates the functionality of fread/fwrite/fseek.
 * Seeks and O_APPEND are not compatible with subsequent streaming filters which create
 * variable size blocks. (eg. compression).
 *
 * Note that Buffered could support O_DIRECT files if the last block is padded and any
 * metadata is stored in a separate location. This is a future goal.
 *
 * Some logical assertions about blocks and file position.
 *    1) All I/Os to actual file are block aligned.  ("actual file" means next stage in pipeline.)
 *    2) All I/Os transfer an entire block, except for the final block in the file.
 *    3) The actual file is pre-positioned for the next I/O.
 *       a) If the buffer is partial or dirty, the actual file position matches bufPosition.
 *       b) If the buffer is full of clean data, the actual file position matches bufPosition+encryptSize.
 *
 *  One goal is to ensure purely sequential reads/writes do not require Seek operations.
 *
 * TODO: Open should return a new instance each time, so we can open multiple files at once.
 */
#include <stdlib.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <assert.h>
#include "iostack_error.h"
#include "common/passThrough.h"
#include "common/debug.h"

#include "file/buffered.h"

#define palloc malloc

/**
 * Structure containing the state of the stream, including its buffer.
 */
struct Buffered
{
    Filter filter;        /* Common to all filters */
    size_t suggestedSize; /* The suggested buffer size. We may make it a bit bigger */

    size_t blockSize;     /* The size of blocks we read/write to our successor. */
    Byte *buf;            /* Local buffer, precisely one block in size. */
    bool dirty;           /* Does the buffer contain dirty data? */

    size_t position;      /* Current byte position in the file. */
    size_t bufPosition;   /* Byte position of the beginning of the buffer */
    size_t bufActual;     /* Nr of actual bytes in the buffer */

    size_t fileSize;      /* Highest byte position we've seen so far for the file. */
    bool sizeConfirmed;   /* fileSize is confirmed as actual file size. */

    bool readable;        /* Opened for reading */
    bool writeable;       /* Opened for writing */
};


/* Forward references */
size_t bufferedSeek(Buffered *this, size_t position, Error *error);
static size_t copyOut(Buffered *this, Byte *buf, size_t size);
static size_t copyIn(Buffered *this, const Byte *buf, size_t size);
static bool flushBuffer(Buffered *this, Error *error);
static bool fillBuffer(Buffered *this, Error *error);
size_t directWrite(Buffered *this, const Byte *buf, size_t size, Error *error);
size_t directRead(Buffered *this, Byte *buf, size_t size, Error *error);

/**
 * Open a buffered file, reading, writing or both.
 */
Buffered *bufferedOpen(Buffered *pipe, const char *path, int oflags, int perm, Error *error)
{
    /* Below us, we need to read/modify/write even if write only. */
    if ( (oflags & O_ACCMODE) == O_WRONLY)
        oflags = (oflags & ~O_ACCMODE) | O_RDWR;

    /* Open the downstream file and clone ourselves */
    Filter *next = passThroughOpen(pipe, path, oflags, perm, error);
    Buffered *this = bufferedNew(pipe->suggestedSize, next);
    if (isError(*error))
        return this;

    /* Are we read/writing or both? */
    this->readable = (oflags & O_ACCMODE) != O_WRONLY;
    this->writeable = (oflags & O_ACCMODE) != O_RDONLY;

    /* Position to the start of file */
    this->position = 0;
    this->bufPosition = 0;

    /* Start with an empty buffer */
    this->dirty = false;
    this->bufActual = 0;

    /* We don't know the size of the file yet. */
    this->fileSize = 0;
    this->sizeConfirmed = (oflags & O_TRUNC) == O_TRUNC;

    /* We don't know block size yet, so we will allocate buffer in the Size event */
    this->buf = NULL;

    return this;
}


/**
 * Write data to the buffered file.
 */
size_t bufferedWrite(Buffered *this, const Byte *buf, size_t size, Error* error)
{
    debug("bufferedWrite: size=%zu  position=%zu \n", size, this->position);
    assert(size > 0);
    if (isError(*error))
        return 0;

    /* If we are at end of current buffer. */
    if (this->position == this->bufPosition + this->blockSize)
    {
        /* Clean the buffer if dirty */
        flushBuffer(this, error);

        /* Move to the next block (with empty buffer) */
        this->bufPosition += this->blockSize;
        this->bufActual = 0;
    }

    /* If buffer is empty, position is aligned, and the data exceeds block size, write direct to next stage */
    if (this->bufActual == 0 && this->position == this->bufPosition && size >= this->blockSize)
        return directWrite(this, buf, size, error);

    /* If buffer is empty ... */
    if (this->bufActual == 0 && this->readable)
    {
        /* Fill the buffer, ignoring EOF */
        fillBuffer(this, error);
        if (errorIsEOF(*error))
            *error = errorOK;
    }

    /* If we are dirtying a clean buffer, then seek backwards to the start of buffer */
    if (!this->dirty)
        passThroughSeek(this, this->bufPosition, error);

    /* Copy data in and update position */
    size_t actual = copyIn(this, buf, size);
    this->dirty = true;
    this->position += actual;
    this->dirty = true;

    assert(actual > 0);
    return actual;
}


/*
 * Optimize writes by going directly to the next file if we don't need buffering.
 */
size_t directWrite(Buffered *this, const Byte *buf, size_t size, Error *error)
{
    /* Write out multiple blocks, but no partials */
    size_t alignedSize = sizeRoundDown(size, this->blockSize);
    size_t actual = passThroughWriteAll(this, buf, alignedSize, error);

    /* Update positions */
    this->position += actual;
    this->bufPosition = sizeRoundDown(this->position, this->blockSize);

    return actual;
}

/**
 * Read bytes from the buffered stream.
 * Note it may take multiple reads to get all the data or to reach EOF.
 */
size_t bufferedRead(Buffered *this, Byte *buf, size_t size, Error *error)
{
    debug("bufferedRead: position=%zu size=%zu encryptSize=%zu\n", this->position, size, this->blockSize);
    if (!errorIsOK(*error))
        return 0;

    /* If we are at the end of the current (non-empty) buffer */
    if (this->position == this->bufPosition + this->bufActual && this->bufActual > 0)
    {
        /* If the buffer is partial, then we are EOF */
        if (this->bufActual < this->blockSize)
            return setError(error, errorEOF);

        /* Clean the buffer if dirty */
        flushBuffer(this, error);

        /* Advance to the next buffer position, with an empty buffer */
        this->bufPosition += this->blockSize;
        this->bufActual = 0;
    }

    /* Optimization. See if we can skip our buffer and talk directly to the next stage */
    if (this->position == this->bufPosition && size > this->blockSize && this->bufActual == 0)
        return directRead(this, buf, size, error);

        /* If our buffer is empty fill it in.  Exit on error or EOF */
    if (this->bufActual == 0 && fillBuffer(this, error))
        return 0;

    /* Copy bytes out from our internal buffer. */
    size_t actual = copyOut(this, buf, size);
    this->position += actual;

    /* Return the number of bytes transferred. */
    debug("bufferedRead: actual=%zu\n", actual);
    return actual;
}


size_t directRead(Buffered *this, Byte *buf, size_t size, Error *error)
{
    debug("directRead: size=%zu  position=%zu encryptSize=%zu\n", size, this->position, this->blockSize);
    /* Read multiple blocks, but no partials */
    size_t alignedSize = sizeRoundDown(size, this->blockSize);
    size_t actual = passThroughReadAll(this, buf, alignedSize, error);

    /* If we read a partial block, claw it back from the caller's buffer */
    size_t actualPartial = actual % this->blockSize;
    size_t actualBlock = actual - actualPartial;
    if (actualPartial > 0)
        copyIn(this, buf+actualBlock, actualPartial);

    /* Update positions */
    this->position += actualBlock;
    this->bufPosition += actualBlock;
    if (actualPartial > 0)
        this->fileSize = this->position + actual;

    debug("directRead: actual=0x%zu\n", actualBlock);
    return actualBlock;
}


/**
 * Seek to a position
 */
size_t bufferedSeek(Buffered *this, size_t position, Error *error)
{
    debug("bufferedSeek: this->position=%zu  position=%lld\n", this->position, (off_t)position);
    if (isError(*error))
        return this->position;

    /* If seeking to end, ... */
    if (position == FILE_END_POSITION)
    {
        /* Clean our buffer if needed. TODO: KLUDGE get file size without losing the dirty data */
        flushBuffer(this, error);
        this->bufPosition = FILE_END_POSITION;  /* An invalid position so we will always seek */

        /* Get the actual file size, and position ourselves at end of last full block */
        this->fileSize = passThroughSeek(this, FILE_END_POSITION, error);
        position = this->fileSize;
    }

    /* If we are moving to a different block ... */
    size_t newBlock = sizeRoundDown(position, this->blockSize);
    debug("bufferedSeek: position=%zu  newBlock=%zu bufPosition=%zu\n", position, newBlock, this->bufPosition);
    if (newBlock != this->bufPosition)
    {
        /* If dirty, flush current block. */
        flushBuffer(this, error);

        /* Position to new block in file. */
        passThroughSeek(this, newBlock, error);
        this->bufPosition = newBlock;
        this->bufActual = 0;
    }

    /* Update position */
    this->position = position;

    return position;
}

/**
 * Close the buffered file.
 */
void bufferedClose(Buffered *this, Error *error)
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
void bufferedSync(Buffered *this, Error *error)
{
    /* Flush our buffers. */
    flushBuffer(this, error);

    /* Pass on the sync request */
    passThroughSync(this, error);
}


/**
 * Negotiate buffer sizes needed by neighboring filters.
 * Since our primary purpose is to resolve block size differences, we handle
 * any size (single byte or larger) from upstream, and we match a multiple of
 * whatever block size is required downstream.
 */
size_t bufferedBlockSize(Buffered *this, size_t prevSize, Error *error)
{
    /* Suggest a block size bigger than what is requested of us. */
    size_t suggestedSize = sizeMax(this->suggestedSize, prevSize);

    /* We have a suggested block size. Pass that on. */
    size_t requestedSize = passThroughBlockSize(this, suggestedSize, error);

    /* Our actual size will be a multiple of the requested size */
    this->blockSize = sizeRoundUp(suggestedSize, requestedSize);

    /* Allocate a buffer of the negotiated size. */
    this->buf = malloc(this->blockSize);
    this->bufActual = 0;

    /* We are buffering, so tell the caller we can accept any size. */
    return 1;
}


FilterInterface bufferedInterface = (FilterInterface)
    {
         .fnOpen = (FilterOpen)bufferedOpen,
         .fnWrite = (FilterWrite)bufferedWrite,
         .fnClose = (FilterClose)bufferedClose,
         .fnRead = (FilterRead)bufferedRead,
         .fnSync = (FilterSync)bufferedSync,
         .fnBlockSize = (FilterBlockSize)bufferedBlockSize,
         .fnSeek = (FilterSeek)bufferedSeek,
    } ;


/**
 Create a new buffer filter object.
 It converts input bytes to records expected by the next filter in the pipeline.
 */
Buffered *bufferedNew(size_t suggestedSize, void *next)
{
    Buffered *this = palloc(sizeof(Buffered));
    *this = (Buffered){0};

    /* Set the suggested buffersize, defaulting to 16Kb */
    if (suggestedSize == 0)
        this->suggestedSize = 16*1024;
    else
        this->suggestedSize = suggestedSize;

    return filterInit(this, &bufferedInterface, next);
}



/*
 * Clean a dirty buffer by writing it to disk. Does not change the contents of the buffer.
 */
static bool flushBuffer(Buffered *this, Error *error)
{
    debug("flushBuffer: position=%zu  bufActual=%zu  dirty=%d\n", this->position, this->bufActual, this->dirty);

    /* if the buffer is dirty, flush it. We reestablish assertion 3a */
    if (this->dirty && this->bufActual > 0)
        passThroughWriteAll(this, this->buf, this->bufActual, error);
    this->dirty = false;

    /* Update file size */
    this->fileSize = sizeMax(this->fileSize, this->bufPosition + this->bufActual);

    return isError(*error);
}

/*
 * Read in a new buffer of data for the current position
 */
static bool fillBuffer (Buffered *this, Error *error)
{
    assert(!this->dirty);
    debug("fillBuffer: bufActual=%zu  bufPosition=%zu sizeConfirmed=%d  fileSize=%zu\n",
          this->bufActual, this->bufPosition, this->sizeConfirmed, this->fileSize);

    /* Quick check for EOF (without system calls) */
    /* TODO */

    /* Read in the current buffer */
    this->bufActual = passThroughReadAll(this, this->buf, this->blockSize, error);

    /* if EOF or partial read, update the known file size */
    /* TODO: */

    return isError(*error);
}


/* Copy user data from the user, respecting boundaries */
static size_t copyIn(Buffered *this, const Byte *buf, size_t size)
{
    /* Copy bytes into the buffer, up to end of data or end of buffer */
    size_t offset = this->position - this->bufPosition;
    size_t actual = sizeMin(this->blockSize - offset, size);
    memcpy(this->buf + offset, buf, actual);
    debug("copyIn: size=%zu bufPosition=%zu bufActual=%zu offset=%zu  actual=%zu\n",
          size, this->bufPosition, this->bufActual, offset, actual);

    /* We may have extended the total data held in the buffer */
    if (actual + offset > this->bufActual)
        this->bufActual = actual + offset;

    assert(this->bufActual <= this->blockSize);
    return actual;
}


/* Copy data to the user, respecting boundaries */
static size_t copyOut(Buffered *this, Byte *buf, size_t size)
{
    size_t offset = this->position - this->bufPosition;
    size_t actual = sizeMin(this->bufActual - offset, size);
    memcpy(buf, this->buf + offset, actual);
    debug("copyOut: size=%zu bufPosition=%zu bufActual=%zu offset=%zu  actual=%zu\n",
          size, this->bufPosition, this->bufActual, offset, actual);
    return actual;
}
