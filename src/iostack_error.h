/***********************************************************************************************************************************
Error handling.
   - Errors can be nested, so one error can show which other error "caused" it.
   - errorOK and errorEOF are allowable values.

Error handling is intended to be the "short circuit" nature, where an
error variable is both input and output to a function.
 - If the error is already set, the function should do nothing and return.
 - If an error occurs, the other return values should be benign.

As a quick prototype, we are NOT doing alloc/free of memory. Consequently,
  - Errors must contain static strings.
  - Nested errors are not supported yet.
Probably need to manage own memory since errors could occur on malloc/free failures.

This is a "header only" file.
***********************************************************************************************************************/
#ifndef FILTER_ERROR_H
#define FILTER_ERROR_H

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* This file is included everywhere, so define Byte here for now. */
typedef unsigned char Byte;

/* An error type which can be tested for OK or not */
typedef struct Error {
    int code;                  /* negative is system, positive is enum.  zero is OK. */
    const char *msg;           /* text message. (which context? static or must be freed?) */
    struct Error *causedBy;    /* points to nested error (or NULL) which caused this one. */
} Error;

/* Some possible enum values for the error code. Negative values are system errors. */
typedef enum ErrorCode {
    errorCodeOK = 0,
    errorCodeEOF,
    errorCodeIoStack
} ErrorCode;

/* Testing the type of error */
inline static bool errorIsOK(Error error) {return error.code == errorCodeOK;}
inline static bool isError(Error error) {return !errorIsOK(error);}
inline static bool errorIsEOF(Error error) {return error.code == errorCodeEOF; }
inline static bool errorIsSystem(Error error) {return error.code < 0;}

/* Access functions */
inline static const char* errorGetMsg(Error error) {return error.msg;}
inline static int errorGetErrno(Error error) {return errorIsSystem(error)? -error.code: 0;}

/* Predefined errors. */
static const Error errorOK = {.code = errorCodeOK, .msg="OK - no error"};
static const Error errorEOF = {.code = errorCodeEOF, .msg = "EOF"};
inline static Error systemError() {return (Error){.code=-errno, .msg=strerror(errno)};}
static const Error errorNotImplemented = (Error){.code=errorCodeIoStack, .msg="Not Implemented"};

/* Set a new error (if none so far) and return 0 in one statement */
inline static int setError(Error *error, Error newError)
{
    if (errorIsOK(*error) || errorIsEOF(*error))
        *error = newError;
    return 0;
}

/* Report a new I/O Stack error */
inline static int ioStackError(Error *error, char *msg)
{
    return setError(error, (Error){.code=errorCodeIoStack, .msg=msg});
}

/* Report a system error (after receiving -1) */
inline static int setSystemError(Error *error)
{
	return setError(error, systemError());
}

#endif /*FILTER_ERROR_H */
