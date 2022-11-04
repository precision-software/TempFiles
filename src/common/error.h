/***********************************************************************************************************************************
Error handling.
   - Errors can be linked, so one error can show which other error "caused" it.
   - errorOK and errorEOF are allowable values.

Error handling is intended to of the "short circuit" nature, where an
error variable is both input and output to a function.
 - If the error is already set, the function should do nothing and return.
 - If an error occurs, the other return values should be benign.

As a quick prototype, errors contain static strings.
  In the long run, errors may need to copy their strings, but we ignore that for the moment.
***********************************************************************************************************************/
#ifndef FILTER_ERROR_H
#define FILTER_ERROR_H

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* This file is included everywhere, so define Byte here for now. */
typedef unsigned char Byte;

#define debug printf
/*#define debug (void) */

/* An error type which can be tested for OK or not */
typedef struct Error {
    int code;                  /* negative is system, positive is enum.  zero is OK. */
    const char *msg;           /* text message. (which context? static or must be freed?) */
    struct Error *causedBy;    /* points to nested error (or NULL) which caused this one. */
} Error;

/* Some possible enum values for the error code. */
typedef enum ErrorCode {
    errorCodeOK = 0,
    errorCodeEOF,
    errorCodeFilter
} ErrorCode;

/* Testing the type of error */
inline static bool errorIsOK(Error error) {return error.code == errorCodeOK;}
inline static bool isError(Error error) {return !errorIsOK(error);}
inline static bool errorIsEOF(Error error) {return error.code == errorCodeEOF; }
inline static bool errorIsSystem(Error error) {return error.code < 0;}

/* Predefined errors. */
static const Error errorOK = {.code = errorCodeOK, .msg="OK - no error"};
static const Error errorEOF = {.code = errorCodeEOF, .msg = "EOF"};
inline static Error systemError() {return (Error){.code=-errno, .msg=strerror(errno)};}
static const Error errorNotImplemented = (Error){.code=errorCodeFilter, .msg="Not Implemented"};

#endif /*FILTER_ERROR_H */
