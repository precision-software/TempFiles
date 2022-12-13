/* */
/* Created by John Morris on 11/1/22. */
/* */
#include <stdlib.h>
#include <lz4frame.h>
#include "common/debug.h"
#include "common/filter.h"
#include "compress/lz4/lz4.h"
#include "common/error.h"
#include "common/passThrough.h"
#include "file/fileSource.h"
#include "file/fileSystemSink.h"
#include "file/buffered.h"

/* Forward references */
static bool isErrorLz4(size_t size, Error *error);
size_t lz4DecompressBuffer(Lz4Compress *this, Byte *out, size_t outSize, Byte *in, size_t inSize, Error *error);
size_t lz4CompressBuffer(Lz4Compress *this, Byte *out, size_t outSize, Byte *in, size_t inSize, Error *error);
size_t compressedSize(size_t size);


/* Structure holding the state of our compression/decompression filter. */
struct Lz4Compress
{
    Filter filter;

    size_t recordSize;                /* Configured size of uncompressed record. */
    size_t compressedSize;            /* upper limit on compressed record size */
    Byte *buf;                        /* Buffer to hold compressed data */
    size_t bufActual;                 /* The amount of compressed data in buffer */

    FileSource *indexFile;            /* Index file created "on the fly" to support record seeks. */
    pos_t compressedPosition;         /* The offset of the current compressed record within the compressed file */

    LZ4F_preferences_t preferences;   /* Choices for compression. */

};


Error lz4CompressOpen(Lz4Compress *this, char *path, int oflags, int mode)
{
    /* Open the compressed file */
    Error error = passThroughOpen(this, path, oflags, mode);

    /* Open the index file as well. */
    /* TODO: We should implement clone on open and use the same pipeline */
    char indexName[MAXPGPATH];
    strlcpy(indexName, path, sizeof(indexName));
    strlcat(indexName, ".idx", sizeof(indexName));
    this->indexFile = fileSourceNew( blockifyNew(1024*1024, fileSystemSinkNew(8)));
    if (errorIsOK(error))
        error = fileOpen(this->indexFile, indexName, oflags, mode);

    /* Do we want to create a header containing the record size? */
    /* TODO: later. */

    return error;
}

size_t lz4CompressBlockSize(Lz4Compress *this, size_t prevSize, Error *error)
{
    /* We send variable sized records to the next stage, so treat as byte stream. */
    size_t nextSize = passThroughBlockSize(this, 1, error);
    if (nextSize != 1)
        return filterError(error, "lz4 Compression has mismatched record size");

    /* Allocate a buffer to hold a compressed record */
    this->compressedSize = compressedSize(this->recordSize);
    this->buf = malloc(this->compressedSize);

    /* Our caller should send us records of this size. */
    return this->recordSize;
}


size_t lz4CompressWrite(Lz4Compress *this, Byte *buf, size_t size, Error *error)
{
    /* Assert: we are 1) at end of previous record, and 2) at end of previous index. */

    /* We write one record at a time */
    size = sizeMin(size, this->recordSize);

    /* Compress the record, and write it out as a variable sized record */
    size_t actual = lz4CompressBuffer(this, this->buf, this->compressedSize, buf, size, error);
    passThroughWriteSized(this, this->buf, actual, error);

    /* Write its offset to the index file */
    filePut8(this->indexFile, this->compressedPosition, error);
    if (isError(*error))
        return 0;

    /* Update our compressedPosition within the compressed file, adding in the length field. */
    this->compressedPosition += (actual + 4);

    /* Assert: We are positioned 1) at end of record, and 2) at end of index entry. */
    return size;
}

size_t lz4CompressRead(Lz4Compress *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    /* Read the index and compressed record together. We need to keep index and file positions in sync */
    Error indexError = errorOK;
    size_t compressedPosition = fileGet8(this->indexFile, &indexError);
    size_t compressedActual = passThroughReadSized(this, this->buf, this->compressedSize, error);
    debug("lz4Read: size=%zu  compresssedPosition=%llu compressedActual=%zu\n", size, this->compressedPosition, compressedActual);

    /* Both the index and compressed file should hit EOF together, and the offsets should match */
    if (errorIsOK(indexError) && errorIsOK(*error) && compressedPosition+compressedActual+4 != this->compressedPosition
       || errorIsEOF(indexError) != errorIsEOF(*error))
        return filterError(error, "lz4: index is inconsistent");

    debug("lz4Read: size=%zu  compresssedPosition=%llu compressedActual=%zu\n", size, this->compressedPosition, compressedActual);
    size_t actual = lz4DecompressBuffer(this, buf, size, this->buf, compressedActual, error);

    if (isError(*error))
        return 0;

    /* Advance our position to reflect the read.*/
    this->compressedPosition += (compressedActual + 4);

    return actual;
}

pos_t lz4CompressSeek(Lz4Compress *this, pos_t position, Error *error)
{
    size_t lastSize = 0;

    /* If seeking to the end, ... */
    if (position == FILE_END_POSITION)
    {
        /* Query the index to figure out how many records are in the file. */
        size_t indexSize = fileSeek(this->indexFile, FILE_END_POSITION, error);
        size_t nrRecords = indexSize / 8;

        /* If the index file contains records, ...*/
        if (nrRecords > 0)
        {
            /* Get offset of the last record */
            fileSeek(this->indexFile, (nrRecords - 1) * 8, error);
            position = fileGet8(this->indexFile, error);

            /* Read the last record */
            lz4CompressSeek(this, position, error);
            lastSize = lz4CompressRead(this, this->buf, this->compressedSize, error);

            /* If the last record was full, then our desired position is at its end. */
            if (lastSize == this->recordSize)
            {
                position += lastSize;
                lastSize = 0;
            }
        }
    }

    /* Verify we are seeking to a record boundary */
    if (position % this->recordSize != 0)
        return filterError(error, "l14 Compression - must seek to a block boundary");

    /* Get the compressed seek position from the index */
    size_t recordNr = position / this->recordSize;
    fileSeek(this->indexFile, recordNr*8, error);
    this->compressedPosition = fileGet8(this->indexFile, error);

    /* Synchronize index to beginning of current record */
    fileSeek(this->indexFile, recordNr*8, error);
    debug("lz4Seek: position=%llu   compressedPosition=%llu   lastSize=%zu\n", position, this->compressedPosition, lastSize);

    /* Seek to the corresponding record */
    passThroughSeek(this, this->compressedPosition, error);

    /* Assert: We are positioned at 1) beginning of record, and 2) beginning of index entry */

    return position + lastSize;
}

void lz4CompressClose(Lz4Compress *this, Error *error)
{
    fileClose(this->indexFile, error);
    passThroughClose(this, error);
    free(this->buf);
    this->buf = NULL;
}


/**
 * Compress a block of data from the input buffer to the output buffer.
 * Note the output buffer must be large enough to hold Size(input) bytes.
 * @param toBuf - the buffer receiving compressed data
 * @param toSize - the size of the buffer
 * @param fromBuf - the buffer providing uncompressed data.
 * @param fromSize - the size of the uncompressed .
 * @param error - keeps track of the error status.
 * @return - the number of compressed bytes.
 *
 * TODO: ensure the context is released if an error occurs.
 */
size_t lz4CompressBuffer(Lz4Compress *this, Byte *toBuf, size_t toSize, Byte *fromBuf, size_t fromSize, Error *error)
{
    debug("lz4CompressBuffer: toSize=%zu fromSize=%zu data=%.*s\n", toSize, fromSize, (int)fromSize, (char*)fromBuf);
    /* Create a compression context */
    LZ4F_cctx *ctx = NULL;
    if (isErrorLz4(LZ4F_createCompressionContext(&ctx, LZ4F_VERSION), error))
        return 0;

    /* Start compression, possibly generating a header. */
    size_t headerSize = LZ4F_compressBegin(ctx, toBuf, toSize, NULL);
    if  (isErrorLz4(headerSize, error))
        return 0;
    toBuf += headerSize;
    toSize -= headerSize;

    /* Compress "fromBuf" into "toBuf", recording any possible error. */
    size_t bodySize = LZ4F_compressUpdate(ctx, toBuf, toSize, fromBuf, fromSize, NULL);
    if (isErrorLz4(bodySize, error))
        return 0;
    toBuf += bodySize;
    toSize -= bodySize;

    /* Finish compressing the buffer */
    size_t tailSize = LZ4F_compressEnd(ctx, toBuf, toSize, NULL);
    if (isErrorLz4(tailSize, error))
        return 0;

    /* release the compression context */
    if (isErrorLz4(LZ4F_freeCompressionContext(ctx), error))
        return 0;

    size_t actual = headerSize + bodySize + tailSize;
    debug("lz4CompressBuffer: actual=%zu   buf=%s\n", actual, asHex(this->buf, actual));
    return actual;
}


/**
 * Decompress a block of data from the input buffer to the output buffer.
 * Note the output buffer must be large enough to hold a full record of data.
 * @param toBuf - the buffer receiving decompressed data
 * @param toSize - the size of the buffer
 * @param fromBuf - the buffer providing compressed data.
 * @param fromSize - the size of the uncompressed .
 * @param error - keeps track of the error status.
 * @return - the number of decompressed bytes.
 */
size_t lz4DecompressBuffer(Lz4Compress *this, Byte *toBuf, size_t toSize, Byte *fromBuf, size_t fromSize, Error *error)
{
    debug("lz4DeompressBuffer: fromSize=%zu toSize=%zu  buf=%s\n", fromSize, toSize, asHex(this->buf, fromSize));
    /* Create a decompression context */
    LZ4F_dctx *dctx;
    if (isErrorLz4(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION), error))
        return 0;

    /* Decompress "fromBuf" into "toBuf", recording any possible error. */
    size_t actualFromSize = fromSize;
    if (isErrorLz4(LZ4F_decompress(dctx, toBuf, &toSize, fromBuf, &actualFromSize, NULL), error))
        return 0;

    /* We should have consumed all the bytes. If not, we probably are generating more output than expected */
    if (actualFromSize != fromSize)
        return filterError(error, "Lz4 decompression didn't fully decompress a buffer");

    /* release the decompression context */
    if (isErrorLz4(LZ4F_freeDecompressionContext(dctx), error))
        return 0;

    debug("lz4DecompressBuffer: toSize=%zu buf=%.*s\n", toSize, (int)toSize, toBuf);
    return toSize;
}


/**
 * Given a proposed buffer of uncompressed data, how large could the compressed data be?
 * For LZ4 compression, it is crucial the output buffer be at least as large as any actual output.
 * @param inSize - size of uncompressed data.
 * @return - maximum size of compressed data.
 */
size_t compressedSize(size_t rawSize)
{
    return LZ4F_compressBound(rawSize, NULL);
}


FilterInterface lz4CompressInterface = (FilterInterface) {
    .fnOpen = (FilterOpen)lz4CompressOpen,
    .fnClose = (FilterClose)lz4CompressClose,
    .fnRead = (FilterRead)lz4CompressRead,
    .fnWrite = (FilterWrite)lz4CompressWrite,
    .fnSeek = (FilterSeek)lz4CompressSeek,
    .fnBlockSize = (FilterBlockSize)lz4CompressBlockSize
};


/**
 * Create a filter for writing and reading compressed files.
 * @param recordSize - size of individually compressed records.
 */
Filter *lz4CompressNew(size_t recordSize, Filter *next)
{
    Lz4Compress *this = malloc(sizeof(Lz4Compress));
    *this = (Lz4Compress){.recordSize = recordSize};
    return filterInit(this, &lz4CompressInterface, next);
}


/*
 * Helper to make LZ4 error handling more concise. Returns true and updates error if an LZ4 error occurred.
 * TODO: We need to copy the message or repeated errors will change it underneath us.
 */
static bool isErrorLz4(size_t size, Error *error) {
    if ((errorIsOK(*error)||errorIsEOF(*error)) && LZ4F_isError(size))
        *error = (Error){.code=errorCodeFilter, .msg=LZ4F_getErrorName(size), .causedBy=NULL};
    return isError(*error);
}
