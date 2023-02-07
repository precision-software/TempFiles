/**/

#include "../iostack_internal.h"


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
		/* Do the next partial read. If eof or error, then done */
		current = fileRead(this, buf + total, size - total, offset + total, ctx);
		if (current <= 0)
			break;
	}

	/* Check for errors. Return the same state as the last fileRead. */
	if (current < 0)
		total = current;

	/* Only report EOF if we really read no data, not just the last read */
	this->eof = (total == 0);
	return total;
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
}

bool fileErrorNext(void *thisVoid)
{
	IoStack *this = thisVoid;
	Assert(this != NULL && this->next != NULL);
	fileErrorInfo(this->next, &this->errNo, this->errMsg);
	this->eof = fileEof(this->next);
	return fileError(this);
}
