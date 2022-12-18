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
 * Some logical assertions about records and file position.
 *    1) All I/Os to the actual file are record aligned.  ("actual file" means next stage in pipeline.)
 *    2) All I/Os transfer an entire record, except for the final record in the file.
 *    3) The actual file is pre-positioned for the next I/O.
 *       a) If the buffer is dirty, the file is positioned at recordPosition,
 *          the beginning of the record, ready to be flushed.
 *       b) If the buffer is clean and has a full record, it is positioned at recordPosition+recordSize,
 *          ready for reading the next record.
 *    4) When a clean buffer is dirtied, the file is repositioned to the beginning of the record to reestablish 3a).
 *       This includes an empty buffer due to a zero length read. (ie not EOF)
 *    5) After reading an EOF, the empty buffer is positioned at recordPosition (position unchanged).
 *    6) After reading a record, the actual file position is > position of the record.
 *       The buffer is marked clean, so any new writes will reposition and overwrite the record. (step 4)
 *       Note this applies to all records, whether full, partial or empty.
 *
 *  One goal is to ensure purely sequential reads/writes do not require Seek operations.
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

    size_t recordSize;     /* The size of blocks we read/write to our successor. */
    Byte *buf;            /* Local buffer, precisely one block in size. */
    bool dirty;           /* Does the buffer contain dirty data? */

    size_t position;      /* Current byte position in the file. */
    size_t blockPosition; /* Byte position of the beginning of the buffer */
    size_t blockActual;   /* Nr of actual bytes in the buffer */

    size_t fileSize;      /* Highest byte position we've seen so far for the file. */
    bool sizeConfirmed;   /* fileSize is confirmed as actual file size. */

    bool readable;        /* Opened for reading */
    bool writeable;       /* Opened for writing */
    bool nextEOF;             /* True if the last downstream read was an EOF */
    bool eof;             /* True if we are to return eof on future reads */
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
Filter *blockifyOpen(Blockify *pipe, char *path, int oflags, int perm, Error *error)
{
    /* Pass the open event to the next filter to actually open the file. */
    Filter *next = passThroughOpen(pipe, path, oflags, perm, error);

    /* Clone ourselves, attaching to the clone of our successor */
    Blockify *this = blockifyNew(pipe->suggestedSize, next);

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

    /* We don't know record size either. Will allocate during size negotiation */
    this->buf = NULL;
    this->nextEOF = false;
    this->eof = false;

    return (Filter*)this;
}


/**
 * Write data to the buffered file.
 */
size_t blockifyWrite(Blockify *this, Byte *buf, size_t size, Error* error)
{
    debug("blockifyWrite: size=%zu  position=%zu nextEOF=%d blockPos=%zu blockActual=%zu\n",
          size, this->position, this->nextEOF, this->blockPosition, this->blockActual);
    assert(size > 0);
    if (isError(*error))
        return 0;

    /* If we are at end of current full sized recod. */
    if (this->position == this->blockPosition + this->recordSize)
    {
        /* Clean the buffer if dirty */
        flushBuffer(this, error);

        /* Move to the next block (with empty buffer) */
        this->blockPosition += this->recordSize;
        this->blockActual = 0;
    }

    /* If we are dirtying a clean buffer, then seek backwards to the start of buffer */
    if (!this->dirty && !this->nextEOF)
        passThroughSeek(this, this->blockPosition, error);

    /* If buffer is empty, position is aligned, and the data exceeds block size, write direct to next stage */
    if (this->blockActual == 0 && this->position == this->blockPosition && size >= this->recordSize)
        return directWrite(this, buf, size, error);

    /* If buffer is empty ... */
    if (this->blockActual == 0 && this->readable)
    {
        /* Fill the buffer, ignoring EOF */
        fillBuffer(this, error);
        if (errorIsEOF(*error))
            *error = errorOK;
    }

    /* Copy data in and update position */
    size_t actual = copyIn(this, buf, size);
    this->position += actual;
    this->dirty = true;

    assert(actual > 0);
    return actual;
}


size_t directWrite(Blockify *this, Byte *buf, size_t size, Error *error)
{
    /* Write out multiple records, but no partials */
    debug("directWrite: size=%zd  position=%zd  nextEOF=%d\n", size, this->position, this->nextEOF);
    size_t alignedSize = sizeRoundDown(size, this->recordSize);
    size_t actual = passThroughWriteAll(this, buf, alignedSize, error);

    /* Update positions */
    this->position += actual;
    this->blockPosition = sizeRoundDown(this->position, this->recordSize);

    return actual;
}

/**
 * Read bytes from the buffered stream.
 * Note it may take multiple reads to get all the data or to reach EOF.
 */
size_t blockifyRead(Blockify *this, Byte *buf, size_t size, Error *error)
{
    debug("blockifyRead: position=%zu size=%zu recordSize=%zu\n", this->position, size, this->recordSize);
    if (!errorIsOK(*error))
        return 0;

    /* If we are at the end of the current (non-empty) buffer */
    if (this->position == this->blockPosition + this->blockActual && this->blockActual > 0)
    {
        /* If the buffer is partial, then we are EOF */
        if (this->blockActual < this->recordSize)
            return setError(error, errorEOF);

        /* Clean the buffer if dirty */
        flushBuffer(this, error);

        /* Advance to the next buffer position, with an empty buffer */
        this->blockPosition += this->recordSize;
        this->blockActual = 0;
    }

    /* Optimization. See if we can skip our buffer and talk directly to the next stage */
    if (this->position == this->blockPosition && size > this->recordSize && this->blockActual == 0)
        return directRead(this, buf, size, error);

    /* If our buffer is empty ... */
    if (this->blockActual == 0)
    {
        /* Fill it in */
        fillBuffer(this, error);

        /* If partial, mark it clean as explained in 5) */
        //this->dirty = errorIsOK(*error) && this->blockActual == this->recordSize;

        /* If we read a zero length record it is EOF, but treat it as clean so we backspace on write. */
        if (errorIsOK(*error) && this->blockActual == 0)
            *error = errorEOF;
    }

    /* Copy bytes out from our internal buffer. */
    size_t actual = copyOut(this, buf, size);
    this->position += actual;

    /* Return the number of bytes transferred. */
    debug("blockifyRead: actual=%zu\n", actual);
    return actual;
}


size_t directRead(Blockify *this, Byte *buf, size_t size, Error *error)
{
    debug("directRead: size=%zu  position=%zu encryptedSize=%zu\n", size, this->position, this->recordSize);
    /* Read multiple records, but no partials */
    size_t alignedSize = sizeRoundDown(size, this->recordSize);
    size_t actual = passThroughReadAll(this, buf, alignedSize, error);
    this->eof = this->nextEOF = errorIsEOF(*error);

    /* If we read a partial block, claw it back from the caller's buffer */
    size_t actualPartial = actual % this->recordSize;
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

    this->eof = false;  /* TODO: comment */

    /* If seeking to end, ... */
    if (position == FILE_END_POSITION)
    {
        /* Clean our buffer if needed. */
        flushBuffer(this, error);
        this->blockActual = 0;

        /* Seek to the best guess EOF (can be last record or EOF) */
        size_t endPosition = passThroughSeek(this, FILE_END_POSITION, error);
        this->blockPosition = sizeRoundDown(endPosition, this->recordSize);

        /* Now read in the final record, if any. Treat EOF like a zero length read. */
        passThroughSeek(this, this->blockPosition, error);
        fillBuffer(this, error);
        if (errorIsEOF(*error))
            *error = errorOK;

        /* Position at the end of the last record */
        this->position = this->blockPosition + this->blockActual;
        return this->position;
    }

    /* If we are moving to a different block ... */
    size_t newBlock = sizeRoundDown(position, this->recordSize);
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
    this->nextEOF = false;

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
    /* Suggest a record size bigger than what is requested of us. */
    size_t suggestedSize = sizeMax(this->suggestedSize, prevSize);

    /* We have a suggested record size. Pass that on. */
    size_t requestedSize = passThroughBlockSize(this, suggestedSize, error);

    /* Our actual size will be a multiple of the requested size */
    this->recordSize = sizeRoundUp(suggestedSize, requestedSize);

    /* Now we know the record size, we can allocate our buffer. */
    this->buf = malloc(this->recordSize);
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
    assert(!this->dirty || this->blockActual == 0);
    this->dirty = false;
    debug("fillBuffer: bufActual=%zu  blockPosition=%zu sizeConfirmed=%d  fileSize=%zu\n",
          this->blockActual, this->blockPosition, this->sizeConfirmed, this->fileSize);

    /* Quick check for EOF (without system calls) */
    /* TODO */

    /* Read in the current buffer */
    this->blockActual = passThroughReadAll(this, this->buf, this->recordSize, error);
    this->nextEOF = errorIsEOF(*error);

    /* update the file size */
    this->fileSize = sizeMax(this->fileSize, this->blockPosition + this->blockActual);
    if (this->blockActual < this->recordSize)
        this->sizeConfirmed = true;

    return isError(*error);
}

static size_t copyIn(Blockify *this, Byte *buf, size_t size)
{
    /* Copy bytes into the buffer, up to end of data or end of buffer */
    size_t offset = this->position - this->blockPosition;
    size_t actual = sizeMin(this->recordSize - offset, size);
    memcpy(this->buf + offset, buf, actual);
    debug("copyIn: size=%zu blockPosition=%zu bufActual=%zu offset=%zu  actual=%zu\n",
          size, this->blockPosition, this->blockActual, offset, actual);

    /* We may have extended the total data held in the buffer */
    if (actual + offset > this->blockActual)
        this->blockActual = actual + offset;

    assert(this->blockActual <= this->recordSize);
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
