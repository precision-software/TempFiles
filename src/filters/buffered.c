/**
 * Buffered reconciles a byte stream input with an output of fixed size blocks.
 * Because output blocks are fixed size, it is possible to do random Seeks
 * and Writes to the output file.
 *
 * Buffered replicates the functionality of fread/fwrite/fseek.
 * Seeks and O_APPEND are not compatible with subsequent streaming filters which create
 * variable size blocks. (eg. compression).
 *
 *  One goal is to ensure purely sequential reads/writes do not require Seek operations.
 */
#include <stdlib.h>
#include <sys/fcntl.h>
#include <assert.h>
#include "../framework/debug.h"
#include "../iostack_internal.h"

#define palloc malloc

/**
 * Structure containing the state of the stream, including its buffer.
 */
 typedef struct Buffered Buffered;

struct Buffered
{
    IoStack iostack;        /* Common to all filters */
    size_t suggestedSize;   /* The suggested buffer size. We may make it a bit bigger */

    Byte *buf;             /* Local buffer, precisely one block in size. */
	size_t bufSize;        /* the size of the local buffer. TODO: rename to bufSize */
    bool dirty;            /* Does the buffer contain dirty data? */

    off_t bufPosition;     /* File position of the beginning of the buffer */
    size_t bufActual;      /* Nr of actual bytes in the buffer */

    off_t fileSize;        /* Highest byte position we've seen so far for the file. */
    bool sizeConfirmed;    /* fileSize is confirmed as actual file size. */

    bool readable;        /* Opened for reading */
    bool writeable;       /* Opened for writing */

	bool *ctx;            /* Remember the context for writing, so we can use it when flushing buffer at close */
};


/* Forward references */
static ssize_t copyOut(Buffered *this, Byte *buf, size_t size, off_t position);
static ssize_t copyIn(Buffered *this, const Byte *buf, size_t size, off_t position);
static bool flushBuffer(Buffered *this, void *ctx);
static bool fillBuffer(Buffered *this, void *ctx);
static ssize_t directWrite(Buffered *this, const Byte *buf, size_t size, off_t offset, void *ctx);
static ssize_t directRead(Buffered *this, Byte *buf, size_t size, off_t offset, void *ctx);
static bool bufferedSeek(Buffered *this, off_t position, void *ctx);

/**
 * Open a buffered file, reading, writing or both.
 */
bool bufferedOpen(Buffered *this, const char *path, int oflags, int perm)
{

	/* Are we read/writing or both? */
	this->readable = (oflags & O_ACCMODE) != O_WRONLY;
	this->writeable = (oflags & O_ACCMODE) != O_RDONLY;

    /* Below us, we need to read/modify/write even if write only. */
    if ( (oflags & O_ACCMODE) == O_WRONLY)
        oflags = (oflags & ~O_ACCMODE) | O_RDWR;
	fileClearError(this);

    /* Open the downstream file */
    if (!fileOpen(nextStack(this), path, oflags, perm))
		return setNextError(this, false);


    /* Position to the start of file with an empty buffer */
    this->bufPosition = 0;
    this->dirty = false;
    this->bufActual = 0;

    /* We don't know the size of the file yet. */
    this->fileSize = 0;
	this->sizeConfirmed = (oflags & O_TRUNC) != 0;

	/* Peek ahead and choo)se a buffer size which is a multiple of our successor's block size */
	this->bufSize = ROUNDUP(this->suggestedSize, nextStack(this)->blockSize);
    this->buf = malloc(this->bufSize);

	/* Close our successor if we couldn't allocate memory */
	if (this->buf == NULL)
	{
		setSystemError(this, -1, "bufferedOpen failed to allocate %z bytes", this->bufSize);
		fileClose(nextStack(this));
		return false;
	}

	/* Success */
	return true;
}


/**
 * Write data to the buffered file.
 */
size_t bufferedWrite(Buffered *this, const Byte *buf, size_t size, off_t offset, void *ctx)
{
    debug("bufferedWrite: size=%zu  offset=%lld \n", size, offset);
    assert(size > 0);

	/* Remember the write context, mainly for flushing the buffer on close, */
	this->ctx = ctx;

	/* Position to the new block if it changed. */
	if (!bufferedSeek(this, offset, ctx))
		return -1;

    /* If buffer is empty, offset is aligned, and the data exceeds block size, write directly to next stage */
    if (this->bufActual == 0 && offset == this->bufPosition && size >= this->bufSize)
        return directWrite(this, buf, size, offset, ctx);

    /* Fill the buffer if it is empty ... */
    if (!fillBuffer(this, ctx))
		return -1;

    /* Copy data into the current buffer */
    size_t actual = copyIn(this, buf, size, offset);

    assert(actual > 0);
    return actual;
}


/*
 * Optimize writes by going directly to the next file if we don't need buffering.
 */
ssize_t directWrite(Buffered *this, const Byte *buf, size_t size, off_t offset, void *ctx)
{
	debug("directWrite: size=%zu offset=%lld\n", size, offset);

    /* Write out multiple blocks, but no partials */
    ssize_t alignedSize = ROUNDDOWN(size, this->bufSize);
    ssize_t actual = fileWrite(nextStack(this), buf, alignedSize, offset, ctx);

	this->fileSize = MAX(this->fileSize, offset + actual);
    return setNextError(this, actual);
}

/**
 * Read bytes from the buffered stream.
 * Note it may take multiple reads to get all the data or to reach EOF.
 */
ssize_t bufferedRead(Buffered *this, Byte *buf, size_t size, off_t offset, void *ctx)
{
	debug("bufferedRead: size=%zu  offset=%lld \n", size, offset);
	assert(size > 0);

	/* Position to the new block if it changed. */
	if (!bufferedSeek(this, offset, ctx))
		return -1;

	/* If buffer is empty, offset is aligned, and the data exceeds block size, write directly to next stage */
	if (this->bufActual == 0 && offset == this->bufPosition && size >= this->bufSize)
		return directRead(this, buf, size, offset, ctx);

	/* Fill the buffer if it is empty */
	if (!fillBuffer(this, ctx))
		return -1;

	/* Copy data from the current buffer */
	ssize_t actual = copyOut(this, buf, size, offset);

	this->iostack.eof = (actual == 0);
	return actual;
}


ssize_t directRead(Buffered *this, Byte *buf, size_t size, off_t offset, void *ctx)
{
	debug("directRead: size=%zu offset=%lld\n", size, offset);
	/* Read multiple blocks, last one might be partial */
	ssize_t alignedSize = ROUNDDOWN(size, this->bufSize);
	ssize_t actual = fileRead(nextStack(this), buf, alignedSize, offset, ctx);

	/* update fileSize */
	if (actual > 0)
        this->fileSize = MAX(this->fileSize, offset + actual);

	return setNextError(this, actual);
}


/**
 * Seek to a position
 */
static bool bufferedSeek(Buffered *this, off_t position, void *ctx)
{
    /* Do nothing if we are already position at the proper block */
    off_t newBlock = ROUNDDOWN(position, this->bufSize);
    debug("bufferedSeek: position=%lld  newBlock=%lld bufPosition=%lld\n", position, newBlock, this->bufPosition);
    if (newBlock == this->bufPosition)
	    return true;

	/* flush current block. */
	if (!flushBuffer(this, ctx))
		return false;

	/* Update position */
	this->bufPosition = newBlock;
	this->bufActual = 0;

    return true;
}

/**
 * Close the buffered file.
 */
bool bufferedClose(Buffered *this)
{
    /* Flush our buffers. */
    bool success = flushBuffer(this, this->ctx);

    /* Pass on the close request., */
    success &= fileClose(nextStack(this));

    this->readable = this->writeable = false;
    if (this->buf != NULL)
        free(this->buf);
	this->buf = NULL;

	debug("bufferedClose(end): success=%d\n", success);
	return success;
}


/*
 * Synchronize any written data to persistent storage.
 */
bool bufferedSync(Buffered *this, void *ctx)
{
    /* Flush our buffers. */
    bool success = flushBuffer(this, ctx);

    /* Pass on the sync request, even if flushing fails. */
    success &= fileSync(nextStack(this), ctx); /* TODO: fileErrorNext */

	if (!fileError(this))
		setNextError(this, success);

	return success;
}

/*
 * Truncate the file at the given offset
 */
bool bufferedTruncate(Buffered *this, off_t offset)
{
	/* Position our buffer with the given position */
	if (!bufferedSeek(this, offset, this->ctx))
	    return false;

	/* Truncate the underlying file */
	if (!fileTruncate(nextStack(this), offset))
		return setNextError(this, false);

	/* Update our buffer so it ends at that position */
	this->bufActual = offset - this->bufPosition;
	this->fileSize = offset;
	if (this->bufActual == 0)
		this->dirty = false;

	return true;
}

off_t bufferedSize(Buffered *this)
{
	if (this->sizeConfirmed)
		return this->fileSize;

	if (!flushBuffer(this, this->ctx))
		return -1;

	off_t size = fileSize(nextStack(this));
	return setNextError(this, size);
}


IoStackInterface bufferedInterface = (IoStackInterface)
	{
		.fnOpen = (IoStackOpen) bufferedOpen,
		.fnWrite = (IoStackWrite) bufferedWrite,
		.fnClose = (IoStackClose) bufferedClose,
		.fnRead = (IoStackRead) bufferedRead,
		.fnSync = (IoStackSync) bufferedSync,
		.fnTruncate = (IoStackTruncate) bufferedTruncate,
		.fnSize = (IoStackSize) bufferedSize,
	};


/**
 Create a new buffer filter object.
 It converts input bytes to records expected by the next filter in the pipeline.
 */
IoStack *bufferedNew(size_t suggestedSize, void *next)
{
	/* TODO: add buffer to end of struc, so we don't need to free */
    Buffered *this = palloc(sizeof(Buffered));
    *this = (Buffered)
		{
		.suggestedSize = (suggestedSize == 0)? 16*1024: suggestedSize,
		.iostack = (IoStack)
			{
			.next = next,
			.iface = &bufferedInterface,
			.blockSize = 1,
			}
		};

    return (IoStack *)this;
}



/*
 * Clean a dirty buffer by writing it to disk. Does not change the contents of the buffer.
 */
static bool flushBuffer(Buffered *this, void *ctx)
{
    debug("flushBuffer: bufPosition=%lld  bufActual=%zu  dirty=%d\n", this->bufPosition, this->bufActual, this->dirty);
	assert(this->bufPosition % this->bufSize == 0);

    /* Clean the buffer if dirty */
	if (this->dirty)
	{
		if (fileWriteAll(nextStack(this), this->buf, this->bufActual, this->bufPosition, ctx) < 0)
			return setNextError(this, false);

		/* Update file size */
		this->fileSize = MAX(this->fileSize, this->bufPosition + this->bufActual);
		this->dirty = false;
	}

    return true;
}

/*
 * Read in a new buffer of data for the current position
 */
static bool fillBuffer (Buffered *this, void *ctx)
{
	debug("fillBuffer: bufActual=%zu  bufPosition=%lld sizeConfirmed=%d  fileSize=%lld\n",
		  this->bufActual, this->bufPosition, this->sizeConfirmed, this->fileSize);
	assert(this->bufPosition % this->bufSize == 0);

	/* Don't fill in if it is already filled in */
	if (this->bufActual > 0)
		return true;

	/* Quick check for EOF (without system calls) */
	if (this->sizeConfirmed && this->bufPosition == this->fileSize)
	{
		this->bufActual = 0;
		this->iostack.eof = true;
		return true;
	}

	/* Check for holes */
	if (this->sizeConfirmed && this->bufPosition > this->fileSize)
		return setIoStackError(this, "buffereedStack: creating holes (offset=%lld, fileSize=%lld", this->bufPosition, this->fileSize);

	/* Read in the current buffer */
	this->bufActual = fileReadAll(nextStack(this), this->buf, this->bufSize, this->bufPosition, ctx);
	if (this->bufActual < 0)
		return setNextError(this, false);

	/* if EOF or partial read, update the known file size */
	this->sizeConfirmed |= (this->bufActual < this->bufSize);
	this->fileSize = MAX(this->fileSize, this->bufPosition + this->bufActual);

	return true;
}

/* Copy user data from the user, respecting boundaries */
static ssize_t copyIn(Buffered *this, const Byte *buf, size_t size, off_t position)
{
	debug("copyIn: position=%lld  size=%zu bufPosition=%lld bufActual=%zu\n", position, size, this->bufPosition, this->bufActual);
	assert(this->bufPosition == ROUNDDOWN(position, this->bufSize));

    /* Check to see if we are creating holes. */
	if (position > this->bufPosition + this->bufActual)
	{
		setIoStackError(this, "Buffered I/O stack would create a hole");
		return -1;
	}

	/* Copy bytes into the buffer, up to end of data or end of buffer */
	off_t offset = position - this->bufPosition;
    ssize_t actual = MIN(this->bufSize - offset, size);
    memcpy(this->buf + offset, buf, actual);
	this->dirty = true;

    /* We may have extended the total data held in the buffer */
    this->bufActual = MAX(this->bufActual, actual + offset);
	debug("copyin(end): actual=%zu  bufActual=%zu\n", actual, this->bufActual);

    assert(this->bufActual <= this->bufSize);
    return actual;
}


/* Copy data to the user, respecting boundaries */
static ssize_t copyOut(Buffered *this, Byte *buf, size_t size, off_t position)
{
	/* Check to see if we are skipping over holes */
	size_t offset = position - this->bufPosition;
	if (offset > this->bufActual)
	{
		setIoStackError(this, "Buffered I/O stack hole");
		return -1;
	}

	/* Copy bytes out of the buffer, up to end of data or end of buffer */
    ssize_t actual = MIN(this->bufActual - offset, size);
    memcpy(buf, this->buf + offset, actual);
    debug("copyOut: size=%zu bufPosition=%lld bufActual=%zu offset=%zu  actual=%zu\n",
          size, this->bufPosition, this->bufActual, offset, actual);
    return actual;
}
