//
// Created by John Morris on 10/13/22.
//

#include "compress/lz4Decompress.h"
#include <lz4frame.h>
#include "compress/lz4Compress.h"
#include "filter/buffer.h"
#include "filter/passThrough.h"
#include "filter/filter.h"

struct Lz4DecompressFilter
{
    Filter header;
    LZ4F_preferences_t preferences;
    size_t blockSize;   // uncompressed block size
    size_t bufferSize;  // big enough to hold max compressed block size
    Buffer *buf;        // our buffer.
    LZ4F_dctx *dctx;    // Pointer to an LZ4 context structure, managed by the lz4 library.
    Error error;
};

const Error errorLZ4ContextFailed = (Error){.code=errorCodeFilter, .msg="Unable to create LZ4 context", .causedBy=NULL};

Filter *
lz4DeompressFilterNew(void *next, size_t blockSize)
{

    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.autoFlush = 1;

    // Get the maximum size of a compressed block.  TODO: must exceed size of frame header.
    size_t bufferSize = LZ4F_compressBound(blockSize, &preferences);

    Lz4DecompressFilter *this = malloc(sizeof(Lz4DecompressFilter));
    *this = (Lz4DecompressFilter){
            .header = (Filter){
                    .next = next,
                    .iface = &lz4DecompressInterface
            },
            .blockSize = blockSize,
            .bufferSize = bufferSize,
            .preferences = preferences,
            .buf = bufferNew(bufferSize)
    };

    // LZ4F manages memory allocation within the context. We provide a pointer to the pointer which it then updates.
    // Allow it to fail silently, and we'll catch it later at first open.  (will ptr be NULL?)
    LZ4F_createDecompressionContext(&this->dctx, LZ4F_VERSION);

    return (Filter*)this;
}

static const Error errorLz4NeedsOutputBuffering =
        (Error){.code=errorCodeFilter, .msg="LZ4 Decompression only outputs to buffered next stage", .causedBy=NULL};

void
lz4DecompressOpen(Lz4DecompressFilter *this, OpenRequest *req)
{
    // Pass the request on, although we should consider adding ".lz4" to the file name.
    passThroughOpen(this, req);

    // Verify our next stage is buffered. Our output size will vary from block to block, so next stage must buffer us.
    if (errorIsOK(req->error) && req->blockSize != 1)
        req->error = errorLz4NeedsOutputBuffering;

    // Generate the frame header.
    assert(bufferIsEmpty(this->buf));
    size_t size = LZ4F_compressBegin(this->dctx, this->buf->buf, this->bufferSize, &this->preferences);
    // TODO: check for lz4 error.

    // Flush the frame header, so we start with an empty buffer.
    this->buf->writePtr += size;
    req->error = bufferForceFlush(this->buf, this);
}

// Disable buffering in the lz4 library.
static const LZ4F_compressOptions_t compressOptions = {.stableSrc=1};

static const Error errorLz4FailedToDecompress =
        {.code=errorCodeFilter, .msg="LZ4 Failed to compress buffer", .causedBy=NULL};

void
lz4DecompressWrite(Lz4DecompressFilter *this, WriteRequest *req)
{
    assert(bufferIsEmpty(this->buf));

    // Decompress the data into our buffer.
    size_t size = LZ4F_decompressUpdate(this->dctx, this->buf->buf, this->bufferSize, req->buf,
                                      req->bufSize, &compressOptions);

    // Verify the compression went as planned. It always should if our buffer was allocated properly.
    if (LZ4F_isError(size))
    {
        req->error = errorLz4FailedToDecompress;
        return;
    }

    // Write it out to the next filter.
    this->buf->writePtr += size;
    req->error = bufferForceFlush(this->buf, this);
    req->actualSize = req->bufSize;
}


void
lz4DecompressClose(Lz4DecompressFilter *this, CloseRequest *req)
{
    // Generate a frame footer.
    size_t size = LZ4F_compressEnd(this->dctx, this->buf->buf, this->bufferSize, &compressOptions);
    // TODO: check for lz4 error.

    // Flush the footer.
    this->buf->writePtr += size;
    Error flushError = bufferForceFlush(this->buf, this);

    // Pass the "close" down the line so stream is closed properly.
    passThroughClose(this, req);

    // If an error occurred, give the flush error priority over a close error.
    if (!errorIsOK(flushError))
        req->error = flushError;
}

FilterInterface lz4DecompressInterface = {
        .fnOpen = (FilterService)lz4DecompressOpen,
        .fnRead = (FilterService)lz4DecompressRead,
        .fnClose = (FilterService)lz4DecompressClose,

        .fnSync = (FilterService)passThroughSync,
        .fnAbort = (FilterService)passThroughAbort,
        .fnPeek = (FilterService)passThroughPeek,
        .fnWrite = (FilterService)passThroughRead,
        .fnSeek = (FilterService)passThroughSeek,
};
