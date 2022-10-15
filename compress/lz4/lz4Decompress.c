//
// Created by John Morris on 10/13/22.
//

#include "lz4Decompress.h"
#include <lz4frame.h>
#include "lz4Compress.h"
#include "common/buffer.h"
#include "common/passThrough.h"
#include "common/filter.h"

struct Lz4DecompressFilter
{
    Filter header;
    size_t blockSize;   // uncompressed block size
    size_t bufferSize;  // big enough to hold max compressed block size
    Buffer *buf;        // our buffer.
    LZ4F_dctx *dctx;    // Pointer to an LZ4 context structure, managed by the lz4 library.
};

static Error errorLZ4(size) {
    if (!LZ4F_isError(size)) return errorOK; return (Error){.code=errorCodeFilter, .msg=LZ4F_getErrorName(size), .causedBy=NULL};
}
static const Error errorLZ4ContextFailed =
        (Error){.code=errorCodeFilter, .msg="Unable to create LZ4 context", .causedBy=NULL};

Filter *
lz4DecompressFilterNew(Filter *next, size_t bufferSize)
{
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.autoFlush = 1;

    // Pick a buffer size which is a multiple of downstream block, but bigger than 16K.
    bufferSize = sizeRoundUp(bufferSize, next->blockSize);

    Lz4DecompressFilter *this = malloc(sizeof(Lz4DecompressFilter));
    *this = (Lz4DecompressFilter){
            .header = (Filter){
                    .next = next,
                    .iface = &lz4DecompressInterface,
                    .blockSize = 1
            },
            .buf = bufferNew(bufferSize),
            .dctx = NULL
    };

    return (Filter*)this;
}

static const Error errorLz4NeedsOutputBuffering =
        (Error){.code=errorCodeFilter, .msg="LZ4 Decompression only outputs to buffered next stage", .causedBy=NULL};

Error
lz4DecompressOpen(Lz4DecompressFilter *this, char *path, int mode, int perm)
{
    // Pass the request on, although we should consider adding ".lz4" to the file name.
    Error error = passThroughOpen(this, path, mode, perm);

    // LZ4F manages memory allocation within the context. We provide a pointer to the pointer which it then updates.
    if (errorIsOK(error) && LZ4F_isError(LZ4F_createDecompressionContext(&this->dctx, LZ4F_VERSION)))
        error = errorLZ4ContextFailed;

    return error;
}

static const Error errorLz4FailedToDecompress =
        {.code=errorCodeFilter, .msg="LZ4 Failed to decompress buffer", .causedBy=NULL};

size_t
lz4DecompressRead(Lz4DecompressFilter *this, Byte *decompressedBytes, size_t decompressedSize, Error *error)
{
    if (isError(*error))  return 0;

    // Get more data if the buffer is empty.
    *error = bufferFill(this->buf, this);
    if (isError(*error))  return 0;

    // We are taking compressed bytes from our buffer and returning decompressed bytes to our caller.
    size_t compressedSize = this->buf->writePtr - this->buf->readPtr;
    Byte *compressedBytes = this->buf->readPtr;

    // Decompress some of the data and check for errors.
    // "decompressedSize" and "compressedSize" are updated to indicate how many bytes were decompressed.
    // "nextSize is a hint about how large the decompressed buffer should be for next time.
    size_t nextSize = LZ4F_decompress(this->dctx, decompressedBytes, &decompressedSize,
                                      compressedBytes, &compressedSize, NULL);
    if (LZ4F_isError(nextSize))
    {
        *error = errorLZ4(nextSize);
        return 0;
    }

    // Remove compressed data from the internal buffer.  // TODO: DRY
    this->buf->readPtr += compressedSize;
    if (bufferIsEmpty(this->buf)) bufferReset(this->buf);

    // Increase size of this->buf so it holds nextSize bytes.
    // TODO:

    return decompressedSize;
}


void
lz4DecompressClose(Lz4DecompressFilter *this, Error *error)
{
    // Pass the "close" down the line so stream is closed properly.
    passThroughClose(this, error);

    // Free the decompression context
    LZ4F_freeDecompressionContext(this->dctx);
    this->dctx = NULL;
}

FilterInterface lz4DecompressInterface = {
        .fnOpen = (FilterOpen)lz4DecompressOpen,
        .fnRead = (FilterRead)lz4DecompressRead,
        .fnClose = (FilterClose)lz4DecompressClose,
};
