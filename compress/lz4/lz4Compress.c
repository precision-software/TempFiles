//
// Created by John Morris on 10/13/22.
//
#include <lz4frame.h>
#include "compress/lz4/lz4_internal.h"

Error
lz4CompressOpen(Lz4Filter *this, char *path, int mode, int perm)
{
    // Pass the request on, although we should consider adding ".lz4" to the file name.
    Error error = passThroughOpen(this, path, mode, perm);
    if (!errorIsOK(error))
        return error;

    // Verify our next stage is buffered. Our output size will vary from block to block, so next stage must buffer us.
    // Ideally, we would check this in "New()", but it is easier to report errors from here.
    if (this->header.next->blockSize != 1)
        return errorLz4NeedsOutputBuffering;

    // LZ4F allocates its own context. We provide a pointer to the pointer which it then updates.
    size_t size = LZ4F_createCompressionContext(&this->cctx, LZ4F_VERSION);
    if (LZ4F_isError(size))
        return errorLz4BeginFailed;

    // Generate the frame header.
    assert(bufferIsEmpty(this->buf));
    size = LZ4F_compressBegin(this->cctx, this->buf->buf, bufferSize(this->buf), &this->preferences);
    if (LZ4F_isError(size))
        return errorLz4BeginFailed;

    // Flush the frame header, so we start with an empty buffer.
    this->buf->writePtr += size;
    return bufferForceFlush(this->buf, this);
}

// Disable buffering in the lz4 compression library.
static const LZ4F_compressOptions_t compressOptions = {.stableSrc=1};

size_t
lz4CompressWrite(Lz4Filter *this, Byte *uncompressedBytes, size_t uncompressedSize, Error *error)
{
    assert(bufferIsEmpty(this->buf));
    if (!errorIsOK(*error))
        return 0;

    // Convert the uncompressed bytes and store them in our buffer.
    size_t compressedSize = LZ4F_compressUpdate(this->cctx, this->buf->writePtr, this->buf->endPtr - this->buf->writePtr,
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
lz4CompressClose(Lz4Filter *this, Error *error)
{
    Error closeError = errorOK;

    // Generate a frame footer.
    size_t size = LZ4F_compressEnd(this->cctx, this->buf->buf, this->bufferSize, &compressOptions);
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
