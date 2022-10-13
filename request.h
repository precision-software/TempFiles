//
// Created by John Morris on 10/11/22.
//

#ifndef UNTITLED1_REQUEST_H
#define UNTITLED1_REQUEST_H

#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

#include <stdbool.h>
#include <memory.h>
#include <assert.h>

#include "error.h"
#include "filter.h"

// Byte and Error belong elsewhere.
typedef unsigned char Byte;

/* Open a file */
typedef struct OpenRequest {
    const char *path;                                               // (in) path to the file
    int mode;                                                       // (in) modeled after Posix
    int perm;                                                       // (in) modeled after Posix
    size_t blockSize;                                               // (out) preferred block size. Last write can be smaller.
    Error error;                                                    // (out) error state, testable with errorIsOK().
} OpenRequest;

/* Write data to a file */
typedef struct WriteRequest {
    Byte *buf;                                                      // (in) The buffer to write.
    size_t bufSize;                                                 // (in) Number of bytes to write. If zero, flush internal buf.
    size_t actualSize;                                              // (out) Number of bytes transferred, undefined if error.
    Error error;                                                    // (out) Error stqte, test with errorIsOK()
} WriteRequest;

/* Read data from a file. */
typedef struct ReadRequest {
    Byte *buf;                                                      // (in) Where to place the data.
    size_t bufSize;                                                 // (in) The size of the buffer. If zero, fill internal buf.
    size_t actualSize;                                              // (out) Actual nr bytes transferred, undefined if error or eof.
    Error error;                                                    // (out) Error status
    bool eof;                                                       // (out) End of file, no bytes transferred.
} ReadRequest;

/* Peek at the next filter's buffer if it has one. */
typedef struct PeekRequest {
    struct Buffer *srcBuf;
    struct Buffer *sinkBuf;                                         // (out) points to the next filter's internal buffer (or NULL)
    Error error;                                                    // (out)
} PeekRequest;

typedef struct SeekRequest {
    size_t position;
    Error error;
} SeekRequest;

typedef struct SyncRequest {
    Error error;
} SyncRequest;

typedef struct CloseRequest {
    Error error;
} CloseRequest;

typedef struct AbortRequest {
    Error error;
} AbortRequest;



#endif //UNTITLED1_REQUEST_H
