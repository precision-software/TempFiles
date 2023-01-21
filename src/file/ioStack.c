/**
 * IoStack is the origin point for file management events.
 * It presents an fread/fwrite style to the entire pipeline.
 * It doesn't do much on its own, as it is
 * more a placeholder for sending events further down the pipeline.
 */
#include <stdlib.h>
#include <stdarg.h>
#include <sys/fcntl.h>
#include "common/passThrough.h"
#include "common/packed.h"
#include "iostack.h"

struct IoStack {
    Filter filter;
    bool open;
};

/**
 * Open a file, returning error information.
 */
void fileOpen(IoStack *this, const char *path, int oflags, int perm, Error *error)
{
	/* If already opened, raise an error */
	if (this->open)
		return (void)ioStackError(error, "Attemptng to open an IoStack which is already open");

    /* Appending to a file is tricky for encrypted/compressed files. TODO: let following filters decide O_APPEND */
    bool append = (oflags & O_APPEND) != 0;
    oflags &= (~O_APPEND);

    /* Open the downstream file */
    passThroughOpen(this, path, oflags, perm, error);

    /* Negotiate block sizes. We don't place any constraints on block size. */
	passThroughBlockSize(this, 1, error);

    /* If we are appending, then seek to the end. */
    if (append)
		fileSeek(this, FILE_END_POSITION, error);

	/* If we had any problems at all, then abort the open. */
	if (isError(*error))
		fileClose(this, error);

	this->open = errorIsOK(*error);
}

/**
 * Write data to a file.
 */
size_t fileWrite(IoStack *this, const Byte *buf, size_t bufSize, Error *error)
{
    return passThroughWriteAll(this, buf, bufSize, error);
}


/**
 * Read data from a file.
 */
size_t fileRead(IoStack *this, Byte *buf, size_t size, Error *error)
{
    return passThroughReadAll(this, buf, size, error);
}


/*
 * Seek to the last partial block in the file, or EOF if all blocks
 * are full sized. (Think of EOF as a final, empty block.)
 */
off_t fileSeek(IoStack *this, off_t position, Error *error)
{
    return passThroughSeek(this, position, error);
}

/**
 * Close a file.
 */
void fileClose(IoStack *this, Error *error)
{
    if (errorIsEOF(*error))
        *error = errorOK;
    passThroughClose(this, error);
	this->open = false;

}

void fileDelete(IoStack *this, const char *path, Error *error)
{
    passThroughDelete(this, path, error);
}

IoStack *fileClone(IoStack *this)
{
	return ioStackNew(passThroughClone(this));
}

void fileFree(IoStack *this)
{
	passThroughFree(this);
	free(this);
}

size_t fileBlockSize(IoStack *this, size_t blockSize, Error *error)
{
	return passThroughBlockSize(this, blockSize, error);
}

FilterInterface ioStackInterface = (FilterInterface)
	{
	.fnBlockSize = (FilterBlockSize)fileBlockSize,
	.fnClone = (FilterClone)fileClone,
	.fnFree = (FilterFree)fileFree,
	};
/**
 * Create a new File Source for generating File events. Since this is the
 * first element in a pipeline of filters, it is the handle for the entire pipeline.
 */
IoStack *
ioStackNew(void *next)
{
    IoStack *this = malloc(sizeof(IoStack));
    filterInit(this, &ioStackInterface, next);

    this->open = false;

    return this;
}

/*
 * Print a formatted message to the IoStack
 */
bool filePrintf(void *this, Error *error, const char *format, ...)
{
	va_list ap;
	Byte buffer[2048];

	/* Format into a local buffer */
	va_start(ap, format);
	size_t actual = vsnprintf((char*)buffer, sizeof(buffer), format, ap);
	va_end(ap);

	/* Write the buffer out */
	if (actual > 0)
		passThroughWriteAll(this, buffer, actual, error);
	else
		ioStackError(error, "Buffer overflow in filePrintf");

	/* true if there was an error */
    return isError(*error);
}

/*
 * Routines to read/write binary integers to an I/O Stack.
 * The integers are sent in network byte order (big endian)
 */
bool filePut8(void *this, uint64_t value, Error *error)
{
	Byte buf[8]; Byte *bp = buf;
	pack8(&bp, bp + 8, value);
	passThroughWriteAll(this, buf, 8, error);
	return isError(*error);
}

uint64_t fileGet8(void *this, Error *error)
{
	Byte buf[8]; Byte *bp = buf;
	size_t actual = passThroughReadAll(this, buf, 8, error);
	if (!errorIsEOF(*error) && actual != 8)
		return ioStackError(error, "fileGet8 unable to read bytes");
    else
	    return unpack8(&bp, buf+8);
}


bool filePut4(void *this, uint32_t value, Error *error)
{
	Byte buf[4]; Byte *bp = buf;
	pack4(&bp, bp + 4, value);
	passThroughWriteAll(this, buf, 4, error);
	return isError(*error);
}

uint32_t fileGet4(void *this, Error *error)
{
	Byte buf[4]; Byte *bp = buf;
	size_t actual = passThroughReadAll(this, buf, 4, error);
	if (!errorIsEOF(*error) && actual != 4)
		return ioStackError(error, "fileGet4 unable to read bytes");
	else
	    return unpack4(&bp, buf+4);
}

bool filePut2(void *this, uint16_t value, Error *error)
{
	Byte buf[2]; Byte *bp = buf;
	pack2(&bp, bp+2, value);
	passThroughWriteAll(this, buf, 2, error);
	return isError(*error);
}

uint16_t fileGet2(void *this, Error *error)
{
	Byte buf[2]; Byte *bp = buf;
	size_t actual = passThroughReadAll(this, buf, 2, error);
	if (!errorIsEOF(*error) && actual != 2)
		return ioStackError(error, "fileGet8 unable to read bytes");
	else
		return unpack4(&bp, buf+2);
}

bool filePut1(void *this, uint8_t value, Error *error)
{
	passThroughWriteAll(this, &value, 1, error);
	return isError(*error);
}

uint8_t fileGet1(void *this, Error *error)
{
	Byte buf[1];
	passThroughReadAll(this, buf, 1, error);
	return *buf;
}
