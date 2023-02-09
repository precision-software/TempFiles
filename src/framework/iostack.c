/**/

#include "../iostack_internal.h"
#include "../framework/packed.h"


ssize_t fileWriteAll(IoStack *this, const Byte *buf, size_t size, off_t offset, void *ctx)
{
	/* Repeat until the entire buffer is written (or error) */
	ssize_t total, current;
	for (total = 0; total < size; total += current)
	{
		/* Do a partial write */
		current = fileWrite(this, buf + total, size - total, offset + total, ctx);
		if (current <= 0)
			break;
	}

	/* Check for errors. Return the same error state as fileWrite. */
	if (current < 0)
		total = current;

	return total;
}


ssize_t fileReadAll(IoStack *this, Byte *buf, size_t size, off_t offset, void *ctx)
{
	/* Repeat until the entire buffer is read (or EOF or error) */
	ssize_t total, current;
	for (total = 0; total < size; total += current)
	{
		/* if we read a partial block, then we are done */
		if (total % thisStack(this)->blockSize != 0)
			break;

		/* Do the next read. If eof or error, then done */
		current = fileRead(this, buf + total, size - total, offset + total, ctx);
		if (current <= 0)
			break;
	}

	/* Check for errors.. */
	if (current < 0)
		total = current;

	/* Only report EOF if we really read no data, not just the last read */
	this->eof = (total == 0);
	return total;
}


/*
 * Write a 4 byte int in network byte order (big endian)
 */
static bool fileWriteInt32(IoStack *this, uint32_t data, off_t offset, void *ctx)
{
	debug("fileWriteInt32: data=%d  offset=%lld\n", data, offset);
	static Byte buf[4];
	buf[0] = (Byte)(data >> 24);
	buf[1] = (Byte)(data >> 16);
	buf[2] = (Byte)(data >> 8);
	buf[3] = (Byte)data;

	return (fileWrite(this, buf, 4, offset, ctx) == 4);
}

/*
 * Read a 4 byte int in network byte order (big endian)
 */
static bool fileReadInt32(IoStack *this, uint32_t *data, off_t offset, void *ctx)
{
	Byte buf[4];
	if (fileReadAll(this, buf, 4, offset, ctx) != 4)
		return false;

	*data = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
	debug("fileReadInt32: data=%d  offset=%lld\n", *data, offset);
	return true;
}


ssize_t fileWriteSized(IoStack *this, const Byte *buf, size_t size, off_t offset, void *ctx)
{
	assert(size <= MAX_BLOCK_SIZE);

	/* Output the length field first */
	if (!fileWriteInt32(this, size, offset, ctx))
		return -1;

	/* Write out the data */
	return fileWriteAll(this, buf, size, offset+4, ctx);
}



ssize_t fileReadSized(IoStack *this, Byte *buf, size_t size, off_t offset, void *ctx)
{
	assert(size <= MAX_BLOCK_SIZE);

	/* Read the length. Return immediately if EOF or error */
	uint32_t expected;
	ssize_t ret = fileReadInt32(this, &expected, offset, ctx);
	if (ret <= 0)
		return ret;

	/* Validate the length */
	if (expected > MAX_BLOCK_SIZE)
		return setIoStackError(this, "IoStack record length of %x is larger than %z", expected, size);

	/* read the data, including the possiblility of a zero length record. */
	ssize_t actual = fileReadAll(this, buf, expected, offset + 4, ctx);
	if (actual >= 0 && actual != expected)
		return setIoStackError(this, "IoStack record corrupted. Expected %z bytes but read only %z byres", expected, actual);

	return actual;
}

bool fileEof(void *thisVoid)
{
	IoStack *this = thisVoid;
	return this->eof;
}

bool fileError(void *thisVoid)
{
	IoStack *this = thisVoid;
	errno = this->errNo;
	return (errno != 0);
}

void fileClearError(void *thisVoid)
{
	IoStack *this = thisVoid;
	this->errNo = 0;
	this->eof = false;
	strcpy(this->errMsg, "");
}

bool fileErrorInfo(void *thisVoid, int *errNo, char *errMsg)
{
	IoStack *this = thisVoid;
	*errNo = errno = this->errNo;
	strcpy(errMsg, this->errMsg);
	return fileError(this);
}

bool fileErrorNext(void *thisVoid)
{
	IoStack *this = thisVoid;
	Assert(this != NULL && this->next != NULL);
	fileErrorInfo(this->next, &this->errNo, this->errMsg);
	this->eof = fileEof(this->next);
	return fileError(this);
}


void freeIoStack(IoStack *ioStack)
{
	/* Scan down the stack, freeing along the way */
	while (ioStack != NULL)
	{
		IoStack *next = ioStack->next;
		free(ioStack);
		ioStack = next;
	}
}
