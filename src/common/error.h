/***********************************************************************************************************************************
Prototype Error handling.
   - Errors can be nested, with a higher level error "causedBy" a lower level error.
   - errorOK and errorEOF are allowable values.
***********************************************************************************************************************/
#ifndef FILTER_ERROR_H
#define FILTER_ERROR_H

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

// This file is included everywhere, so define Byte here for now.
typedef unsigned char Byte;

#define debug printf
//#define debug (void)

typedef struct Error {
    int code;                                                       // negative is system, positive is enum.  zero is OK.
    const char *msg;                                                // text message. (which context? static or must be freed?)
    struct Error *causedBy;                                         // points to lower level error (or NULL) which caused this one.
} Error;


typedef enum ErrorCode {
    errorCodeOK = 0,
    errorCodeEOF,
    errorCodeFilter
} ErrorCode;

inline static bool errorIsOK(Error error) {return error.code == errorCodeOK;}
inline static bool isError(Error error) {return !errorIsOK(error);}
inline static bool errorIsEOF(Error error) {return error.code == errorCodeEOF; }
inline static bool errorIsSystem(Error error) {return error.code < 0;}

// Predefined errors.

static const Error errorOK = {.code = errorCodeOK, .causedBy=NULL, .msg="OK - no error"};
static const Error errorEOF = {.code = errorCodeEOF, .causedBy=NULL, .msg = "EOF"};
inline static Error systemError() {return (Error){.code=-errno, .msg=strerror(errno), .causedBy=NULL};}

#endif //FILTER_ERROR_H
