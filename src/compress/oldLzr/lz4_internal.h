/* */
/* Created by John Morris on 10/15/22. */
/* */

#ifndef FILTER_LZ4_INTERNAL_H
#define FILTER_LZ4_INTERNAL_H

#include "common/buffer.h"
#include "common/passThrough.h"
#include "common/stage.h"
#include "lz4.h"


struct Lz4Filter
{
    Filter filter;
    bool readable;
    bool writeable;
    size_t blockSize;   /* uncompressed block size */
    size_t bufferSize;  /* big enough to hold max compressed block size */
    Buffer *buf;        /* our buffer. */
    LZ4F_preferences_t preferences;  /* Choices for compression. */
    LZ4F_cctx *cctx;    /* Pointer to an LZ4 context structure, managed by the lz4 library. */
    LZ4F_dctx *dctx;    /* Pointer to an LZ4 context structure, managed by the lz4 library. */
};

extern FilterInterface lz4Interface;

static const Error errorLZ4ContextFailed =
        (Error){.code=errorCodeFilter, .msg="Unable to create LZ4 context", .causedBy=NULL};
static const Error errorLz4NeedsOutputBuffering =
        (Error){.code=errorCodeFilter, .msg="LZ4 Compression only outputs to buffered next stage", .causedBy=NULL};
static const Error errorLz4BeginFailed =
        (Error){.code=errorCodeFilter, .msg="LZ4 couldn't create header", .causedBy=NULL};
static const Error errorLz4FailedToCompress =
        {.code=errorCodeFilter, .msg="LZ4 Failed to compress buffer", .causedBy=NULL};
static const Error errorCantBothReadWrite =
        (Error) {.code=errorCodeFilter, .msg="LZ4 compression can't read and write at the same time", .causedBy=NULL};
static const Error errorLz4FailedToDecompress =
        {.code=errorCodeFilter, .msg="LZ4 Failed to decompress buffer", .causedBy=NULL};

/* TODO: Not really static, but will work if the LZ4 messages persist. */
static Error errorLZ4(size_t size) {
    if (!LZ4F_isError(size)) return errorOK; return (Error){.code=errorCodeFilter, .msg=LZ4F_getErrorName(size), .causedBy=NULL};
}

/* Forward References. */
Error lz4CompressOpen(Lz4Filter *this, char *path, int mode, int perm);
void lz4CompressClose(Lz4Filter *this, Error *error);
Error lz4DecompressOpen(Lz4Filter *this, char *path, int mode, int perm);
void lz4DecompressClose(Lz4Filter *this, Error *error);
size_t lz4CompressWrite(Lz4Filter *this, Byte *buf, size_t size, Error *error);
size_t lz4DecompressRead(Lz4Filter *this, Byte *buf, size_t size, Error *error);

#endif /*FILTER_LZ4_INTERNAL_H */
