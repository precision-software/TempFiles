//
// Created by John Morris on 10/13/22.
//

#include <lz4frame.h>
#include "compress/lz4/lz4_internal.h"
#include "common/buffer.h"
#include "common/passThrough.h"
#include "common/filter.h"

Error
lz4DecompressOpen(Lz4Filter *this, char *path, int mode, int perm)
{
    // Pass the request on, although we should consider adding ".lz4" to the file name.
    Error error = passThroughOpen(this, path, mode, perm);

    // LZ4F manages memory allocation within the context. We provide a pointer to the pointer which it then updates.
    if (errorIsOK(error) && LZ4F_isError(LZ4F_createDecompressionContext(&this->dctx, LZ4F_VERSION)))
        error = errorLZ4ContextFailed;

    return error;
}


size_t
lz4DecompressRead(Lz4Filter *this, Byte *decompressedBytes, size_t decompressedSize, Error *error)
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
lz4DecompressClose(Lz4Filter *this, Error *error)
{
    // Pass the "close" down the line so stream is closed properly.
    passThroughClose(this, error);

    // Free the decompression context
    LZ4F_freeDecompressionContext(this->dctx);
    this->dctx = NULL;
}