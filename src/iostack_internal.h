/***********************************************************************************************************************************
Header file for developers of I/O Stacks

As a quick prototype, we are NOT doing alloc/free of memory. Consequently,
  - Errors must contain static strings.
  - Nested errors are not supported yet.
Probably need to manage own memory since errors could occur on malloc/free failures.

This is a "header only" file.
***********************************************************************************************************************/
#ifndef FILTER_ERROR_H
#define FILTER_ERROR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#ifndef Assert
#include <assert.h>
#define Assert assert
#endif

#include "./iostack.h"

/* TODO: not all are on performance path, so review which should be inlined.*/

/* Set error information and return -1 */
inline static int setError(void *thisVoid, int errNo, const char *fmt, va_list args)
{
	IoStack *this = thisVoid;

	this->errNo = errNo;
	snprintf(this->errMsg, sizeof(this->errMsg), fmt, args);

	/* restore the errno so caller can still test it */
	errno = errNo;
    return -1;
}


/* Report a new I/O Stack error */
inline static ssize_t setIoStackError(void *this, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
    setError(this, EIOSTACK, fmt, ap);
	va_end(ap);

	return -1;
}


/* Check for a  system error, returning the retval */
inline static ssize_t setSystemError(void *this, ssize_t retval, const char *fmt, ...)
{
	/* If a system error occured ... */
	if (retval < 0)
	{
		/* Save the error code */
		int save_errno = errno;

		/* Create a new format string with system error info prepended */
		char newFmt[121];
		snprintf(newFmt, sizeof(newFmt), "(%d - %s) %s", save_errno, strerror(errno), fmt);

		/* Save the error information so "fileError" can retrieve it later */
		va_list ap;
		va_start(ap, fmt);
		setError(this, save_errno, newFmt, ap);
		va_end(ap);
	}

	return retval;
}


bool fileErrorNext(void *thisVoid);

/* Some convenient macros */
#define MAX(a,b) ( ((a)>(b))?(a):(b) )
#define MIN(a,b) ( ((a)<(b))?(a):(b) )
#define ROUNDDOWN(a,b) ( (a) / (b) * (b))
#define ROUNDUP(a,b)    ROUNDDOWN(a + b - 1, b)

#endif /*FILTER_ERROR_H */
