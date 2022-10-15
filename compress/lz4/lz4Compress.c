//
// Created by John Morris on 10/13/22.
//
#include <lz4frame.h>
#include "lz4Compress.h"
#include "common/buffer.h"
#include "common/passThrough.h"
#include "common/filter.h"

struct Lz4CompressFilter
{
    Filter header;
    LZ4F_preferences_t preferences;
    size_t blockSize;   // uncompressed block size
    size_t bufferSize;  // big enough to hold max compressed block size
    Buffer *buf;        // our buffer.
    LZ4F_cctx *cctx;    // Pointer to an LZ4 context structure, managed by the lz4 library.
    Error error;
};

static const Error errorLZ4ContextFailed =
        (Error){.code=errorCodeFilter, .msg="Unable to create LZ4 context", .causedBy=NULL};
static const Error errorLz4NeedsOutputBuffering =
        (Error){.code=errorCodeFilter, .msg="LZ4 Compression only outputs to buffered next stage", .causedBy=NULL};
static const Error errorLz4BeginFailed =
        (Error){.code=errorCodeFilter, .msg="LZ4 couldn't create header", .causedBy=NULL};
static const Error errorLz4FailedToCompress =
        {.code=errorCodeFilter, .msg="LZ4 Failed to compress buffer", .causedBy=NULL};

Filter *
lz4CompressFilterNew(void *next, size_t blockSize)
{

    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.autoFlush = 1;

    // Get the maximum size of a compressed block.  TODO: must exceed size of frame header.
    size_t bufferSize = LZ4F_compressBound(blockSize, &preferences);

    Lz4CompressFilter *this = malloc(sizeof(Lz4CompressFilter));
    *this = (Lz4CompressFilter){
        .header = (Filter){
            .next = next,
            .iface = &lz4CompressInterface,
            .blockSize = 1
        },
        .blockSize = blockSize,
        .bufferSize = bufferSize,
        .preferences = preferences,
        .buf = bufferNew(bufferSize),
        .cctx = NULL
    };

    return (Filter*)this;
}

Error
lz4CompressOpen(Lz4CompressFilter *this, char *path, int mode, int perm)
{
    // Pass the request on, although we should consider adding ".lz4" to the file name.
    Error error = passThroughOpen(this, path, mode, perm);
    if (!errorIsOK(error))
        return error;

    // Verify our next stage is buffered. Our output size will vary from block to block, so next stage must buffer us.
    // Ideally, we would check this in "New()", but it is easier to report errors from here.
    if (this->header.next->blockSize != 1)
        return errorLz4NeedsOutputBuffering;

    // LZ4F manages memory allocation within the context. We provide a pointer to the pointer which it then updates.
    size_t size = LZ4F_createCompressionContext(&this->cctx, LZ4F_VERSION);
    if (LZ4F_isError(size))
        return errorLz4BeginFailed;

    // Generate the frame header.
    assert(bufferIsEmpty(this->buf));
    size = LZ4F_compressBegin(this->cctx, this->buf->buf, bufferSize(this->buf), &this->preferences);
    if (LZ4F_isError(size))
        return errorLz4BeginFailed;

    // Flush the frame header, so we start with an empty buffer.
    this->buf->writePtr += (size_t)size;
    return bufferForceFlush(this->buf, this);
}

// Disable buffering in the lz4 compression library.
static const LZ4F_compressOptions_t compressOptions = {.stableSrc=1};

size_t
lz4CompressWrite(Lz4CompressFilter *this, Byte *uncompressedBytes, size_t uncompressedSize, Error *error)
{
    assert(bufferIsEmpty(this->buf));
    if (!errorIsOK(*error))
        return 0;

    // Convert the uncompressed bytes and store them in our buffer.
    ssize_t compressedSize = LZ4F_compressUpdate(this->cctx, this->buf->writePtr, this->buf->endPtr - this->buf->writePtr,
                                                 uncompressedBytes, uncompressedSize, &compressOptions);

    // Verify the compression went as planned. It always should if our buffer was allocated properly.
    if (LZ4F_isError(compressedSize))
    {
        *error = errorLz4FailedToCompress;
        return 0;
    }

    // Add it to buf and flush it out to the next filter.
    this->buf->writePtr += compressedSize;
    *error = bufferForceFlush(this->buf, this);

    // Return the number of uncompressed bytes we wrote. Since there were no errors, we assume they were all written.
    return uncompressedSize;
}


void
lz4CompressClose(Lz4CompressFilter *this, Error *error)
{
    Error closeError = errorOK;

    // Generate a frame footer.
    ssize_t size = LZ4F_compressEnd(this->cctx, this->buf->buf, this->bufferSize, &compressOptions);
    if (LZ4F_isError(size))
    {
        closeError = errorLz4FailedToCompress;
        size = 0;
    }

    // Flush the footer.
    this->buf->writePtr += size;
    Error flushError = bufferForceFlush(this->buf, this);  // TODO: pass error as a parameter

    // Pass the "close" down the line so stream is closed properly.
    passThroughClose(this, &closeError);

    // release the compression context
    LZ4F_freeCompressionContext(this->cctx);
    this->cctx = NULL;

    // If errors occurred, give the original error priority over the close error.
    if (errorIsOK(*error))
         *error = flushError;
}

FilterInterface lz4CompressInterface = {
        .fnOpen = (FilterOpen)lz4CompressOpen,
        .fnWrite = (FilterWrite)lz4CompressWrite,
        .fnClose = (FilterClose)lz4CompressClose,
};
