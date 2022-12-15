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

    Byte *tempBuf;                    /* temporary buffer to hold decrypted data when probing for size */

    bool previousRead;                /* true if the previous op was a read (or equivaleht) */
};


Filter *lz4CompressOpen(Lz4Compress *this, char *path, int oflags, int mode, Error *error)
{
    /* Open the compressed file */
    Filter *next = passThroughOpen(this, path, oflags, mode, error);

    /* Open the index file as well. */
    char indexName[MAXPGPATH];
    strlcpy(indexName, path, sizeof(indexName));
    strlcat(indexName, ".idx", sizeof(indexName));
    Filter *indexFile = passThroughOpen(this, path, oflags, mode, error);

    /* Make note we are at the start of the compressed file */
    this->compressedPosition = 0;
    this->previousRead = true;

    /* Do we want to create a header containing the record size? */
    /* TODO: later. */

    return lz4CompressNew(this->recordSize, next);
}

size_t lz4CompressBlockSize(Lz4Compress *this, size_t prevSize, Error *error)
{
    /* Starting with the index file, we send 4 byte records */
    size_t indexSize = passThroughBlockSize(this->indexFile, sizeof(pos_t), error);
    if (sizeof(pos_t) % indexSize != 0)
        return filterError(error, "lz4 index file has incompatible record size");

    /* For our data file, we send variable sized records to the next stage, so treat as byte stream. */
    size_t nextSize = passThroughBlockSize(this, 1, error);
    if (nextSize != 1)
        return filterError(error, "lz4 Compression has mismatched record size");

    /* Allocate a buffer to hold a compressed record */
    this->compressedSize = compressedSize(this->recordSize);
    this->buf = malloc(this->compressedSize);
    this->tempBuf = malloc(this->recordSize);

    /* Our caller should send us records of this size. */
    return this->recordSize;
}


size_t lz4CompressWrite(Lz4Compress *this, Byte *buf, size_t size, Error *error)
{
    /* We do one record at a time */
    size = sizeMin(size, this->recordSize);

    debug("lz4Write: size=%zu  compressedPosition=%llu\n", size, this->compressedPosition);

    /* If previous read, synchronize the index by writing out offset to start of current record */
    if (this->previousRead)
        filePut8(this->indexFile, this->compressedPosition, error);
    this->previousRead = false;

    /* Compress the record, and write it out as a variable sized record */
    size_t actual = lz4CompressBuffer(this, this->buf, this->compressedSize, buf, size, error);
    passThroughWriteSized(this, this->buf, actual, error);
    if (isError(*error))
        return 0;

    /* Update our file position */
    this->compressedPosition += (actual + 4);

    /* If we wrote a full block, write out an index entry */
    if (size == this->recordSize)
        filePut8(this->indexFile, this->compressedPosition, error);

    return size;
}

size_t lz4CompressRead(Lz4Compress *this, Byte *buf, size_t size, Error *error)
{
    /* We do one record at a time */
    size = sizeMin(size, this->recordSize);

    debug("lz4Read: size=%zu  compressedPosition=%llu\n", size, this->compressedPosition);
    if (isError(*error))
        return 0;

    /* If last op was a read, read the index so we stay in sync */
    if (this->previousRead)
        fileGet8(this->indexFile, error);
    this->previousRead = true;

    /* Read the compressed record, */
    size_t compressedActual = passThroughReadSized(this, this->buf, this->compressedSize, error);
    if (isError(*error))
        return 0;

    /* Update the compressed file position to be afterr the record. */
    this->compressedPosition += (compressedActual + 4);

    /* Decompress the record we just read. */
    size_t actual = lz4DecompressBuffer(this, buf, size, this->buf, compressedActual, error);

    return actual;
}

pos_t lz4CompressSeek(Lz4Compress *this, pos_t position, Error *error)
{
    debug("lzSeek (start): position=%lld  compressedPosition=%llu\n", position, this->compressedPosition);

    /* If seeking to the end, ... */
    if (position == FILE_END_POSITION)
    {
        /* Query the index to figure out how many records are in the file. */
        size_t indexSize = fileSeek(this->indexFile, FILE_END_POSITION, error);
        size_t nrRecords = indexSize / 8;

        /* if index file is empty, the data file should be as well.  TODO: should we verify? */
        if (nrRecords == 0)
            return 0;

        /* Seek to the final partial record, if any */
        pos_t lastPosition = (nrRecords-1) * this->recordSize;
        lz4CompressSeek(this, lastPosition, error);

        /* read the final partial record, treating EOF like a zero length partial record */
        size_t lastSize = lz4CompressRead(this, this->tempBuf, this->recordSize, error);
        if (errorIsEOF(*error))
            *error = errorOK;

        /* If the last record was partial, then go back to its starting position. */
        if (errorIsOK(*error) && lastSize < this->recordSize)
            lz4CompressSeek(this, lastPosition, error);

        debug("lz4Seek (end of  file): lastPosition=%llu lastSize=%zu  compressedPosition=%llu\n", lastPosition, lastSize, this->compressedPosition);

        /* Done. Return the file size, and we are positioned at end of last full record */
        return lastPosition + lastSize;
    }

    /* otherwise, seeking to a file position */
    /* Verify we are seeking to a record boundary */
    if (position % this->recordSize != 0)
        return filterError(error, "l14 Compression - must seek to a block boundary");

    /* Read from the index to get the position in the compressed file. */
    size_t recordNr = position / this->recordSize;
    fileSeek(this->indexFile, recordNr*8, error);
    this->compressedPosition = fileGet8(this->indexFile, error);

    /* To keep index and data in sync, this is like a write */
    this->previousRead = false;

    /* When seeking to position 0 in a new file, it is OK to get an EOF reading the index */
    if (errorIsEOF(*error) && position == 0)
    {
        this->previousRead = true;
        this->compressedPosition = 0;
        *error = errorOK;
    }

    debug("lz4Seek: position=%llu   compressedPosition=%llu \n", position, this->compressedPosition);

    /* Seek to corresponding record */
    passThroughSeek(this, this->compressedPosition, error);

    /* To keep index and data in sync, this is like a write */
    this->previousRead = false;

    return position;
}

void lz4CompressClose(Lz4Compress *this, Error *error)
{
    fileClose(this->indexFile, error);
    passThroughClose(this, error);
    free(this->buf);
    this->buf = NULL;
    free(this->tempBuf);
    this->tempBuf = NULL;
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
    memset(toBuf, 'Y', toSize);  // debug
    size_t actualFromSize = fromSize;
    size_t ret = LZ4F_decompress(dctx, toBuf, &toSize, fromBuf, &actualFromSize, NULL);
    if (isErrorLz4(ret, error))
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
