//
// Created by John Morris on 10/13/22.
//
#include <lz4frame.h>
#include "compress/lz4Compress.h"
#include "filter/buffer.h"
#include "filter/passThrough.h"
#include "filter/filter.h"

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
    // Allow it to fail silently, and we'll catch it later at first open.  (will ptr be NULL?)
    size_t size = LZ4F_createCompressionContext(&this->cctx, LZ4F_VERSION);
    if (LZ4F_isError(size))
        return errorLz4BeginFailed;

    // Generate the frame header.
    assert(bufferIsEmpty(this->buf));
    size = LZ4F_compressBegin(this->cctx, this->buf->buf, this->bufferSize, &this->preferences);
    if (LZ4F_isError(size))
        return errorLz4BeginFailed;

    // Flush the frame header, so we start with an empty buffer.
    this->buf->writePtr += size;
    return bufferForceFlush(this->buf, this);
}

// Disable buffering in the lz4 compression library.
static const LZ4F_compressOptions_t compressOptions = {.stableSrc=1};

size_t
lz4CompressWrite(Lz4CompressFilter *this, Byte *buf, size_t bufSize, Error *error)
{
    assert(bufferIsEmpty(this->buf));
    if (!errorIsOK(*error))
        return 0;

    // Compress the data into our buffer.
    size_t size = LZ4F_compressUpdate(this->cctx, this->buf->buf, this->bufferSize, buf,
                                      bufSize, &compressOptions);

    // Verify the compression went as planned. It always should if our buffer was allocated properly.
    if (LZ4F_isError(size))
    {
        *error = errorLz4FailedToCompress;
        return 0;
    }

    // Write it out to the next filter.
    this->buf->writePtr += size;
    *error = bufferForceFlush(this->buf, this);

    return size;
}


void
lz4CompressClose(Lz4CompressFilter *this, Error *error)
{
    // Generate a frame footer.
    size_t size = LZ4F_compressEnd(this->cctx, this->buf->buf, this->bufferSize, &compressOptions);
    // TODO: check for lz4 error.

    // Flush the footer.
    this->buf->writePtr += size;
    Error flushError = bufferForceFlush(this->buf, this);

    // Pass the "close" down the line so stream is closed properly.
    passThroughClose(this, error);

    // release the compression context
    LZ4F_freeCompressionContext(this->cctx);

    // If an error occurred, give the flush error priority over a close error.
    if (!errorIsOK(flushError))
         *error = flushError;
}

FilterInterface lz4CompressInterface = {
        .fnOpen = (FilterOpen)lz4CompressOpen,
        .fnWrite = (FilterWrite)lz4CompressWrite,
        .fnClose = (FilterClose)lz4CompressClose,
};
