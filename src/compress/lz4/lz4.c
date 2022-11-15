/* */
/* Created by John Morris on 11/1/22. */
/* */
#include <lz4frame.h>
#include "common/convertFilter.h"
#include "common/converter.h"
#include "compress/lz4/lz4.h"

static const Error errorLZ4ContextFailed =
        (Error){.code=errorCodeFilter, .msg="Unable to create LZ4 context", .causedBy=NULL};
static const Error errorCantBothReadWrite =
        (Error) {.code=errorCodeFilter, .msg="LZ4 compression can't read and write at the same time", .causedBy=NULL};

/*
 * Helper to make LZ4 error handling more concise. Returns true and updates error if an LZ4 error occurred.
 * TODO: We need to copy the message or repeated errors will change it underneath us.
 */
static bool isErrorLz4(size_t size, Error *error) {
    if (errorIsOK(*error) && LZ4F_isError(size))
        *error = (Error){.code=errorCodeFilter, .msg=LZ4F_getErrorName(size), .causedBy=NULL};
    return isError(*error);
}


/* Structure holding the state of our compression/decompression filter. */
typedef struct Lz4Compress
{
    LZ4F_preferences_t preferences;   /* Choices for compression. */
    LZ4F_cctx *cctx;                  /* Pointer to an LZ4 compression context. */
    LZ4F_dctx *dctx;                  /* Pointer to an LZ4 decompression context. */
} Lz4Compress;

/**
 * Start the compression.
 * @param buf - an output buffer to hold any leading headers.
 * @param size - the size of the output buffer.
 * @param error - an error object for tracking errors.
 * @return - the number of bytes generated for the leading header.
 */
size_t lz4CompressBegin(Lz4Compress *this, Byte *buf, size_t size, Error *error)
{
    /* LZ4F allocates its own context. We provide a pointer to the pointer which it then updates. */
    size_t result = LZ4F_createCompressionContext(&this->cctx, LZ4F_VERSION);
    if (isErrorLz4(result, error))
        return 0;

    /* Generate the frame header. */
    size_t actual = LZ4F_compressBegin(this->cctx, buf, size, &this->preferences);
    if (isErrorLz4(actual, error))
        return 0;

    return actual;
}

/**
 * Compress a block of data from the input buffer to the output buffer.
 * Note the output buffer must be large enough to hold Size(input) bytes.
 * @param toBuf - the buffer receiving compressed data
 * @param toSizePtr - pointer to the size of the buffer, getting updated when data is compressed.
 * @param fromBuf - the buffer providing uncompressed data.
 * @param fromSizePtr - the size of the uncompressed, getting updated when data is compressed.
 * @param error - keeps track of the error status.
 */
void lz4CompressConvert(Lz4Compress *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error)
{
    /* Convert the uncompressed bytes from the "fromBuf" into the "toBuf", recording any possible error. */
    size_t toSizeSave = *toSize;
    size_t bound = LZ4F_compressBound(*fromSize, &this->preferences);
    *toSize = LZ4F_compressUpdate(this->cctx, toBuf, *toSize, fromBuf, *fromSize, NULL);
    isErrorLz4(*toSize, error);
}

/**
 * Clean up when compression is finished, outputting any possible trailer info.
 * @param toBuf - buffer to receive trailer data.
 * @param toSize - size of the buffer.
 * @param error - keep track of error status.
 * @return the actual number of compressed bytes in the trailer.
 */
size_t lz4CompressEnd(Lz4Compress *this, Byte *toBuf, size_t toSize, Error *error)
{
    /* Flush remaining data and generate a frame footer if needed. */
    size_t actual = LZ4F_compressEnd(this->cctx, toBuf, toSize, NULL);
    if (isErrorLz4(actual, error))
       return 0;

    /* release the compression context and check for errors. */
    isErrorLz4(LZ4F_freeCompressionContext(this->cctx), error);
    this->cctx = NULL;

    return actual;
}

/**
 * Given a proposed buffer of uncompressed data, how large could the compressed data be?
 * For LZ4 compression, it is crucial the output buffer be at least as large as any actual output.
 * @param inSize - size of uncompressed data.
 * @return - maximum size of compressed data.
 */
size_t lz4CompressSize(Lz4Compress *this, size_t inSize)
{
    return LZ4F_compressBound(inSize, &this->preferences);
}

/**
 * Release any compression resources, including the compresson converter itself.
 * @param error - keep track of errors.
 */
void lz4CompressFree(Lz4Compress *this, Error *error)
{
    /* If not done earlier, release the compression context and check for errors. */
    if (this->cctx != NULL)
        isErrorLz4(LZ4F_freeCompressionContext(this->cctx), error);

    free(this);
}

/**
 * The interface allowing an Lz4Compress to be an abstract converter object.
 */
ConverterIface lz4CompressIface =
{
    .fnSize = (ConvertSizeFn)lz4CompressSize,
    .fnBegin = (ConvertBeginFn)lz4CompressBegin,
    .fnConvert = (ConvertConvertFn)lz4CompressConvert,
    .fnEnd = (ConvertEndFn)lz4CompressEnd,
    .fnFree = (ConvertFreeFn)lz4CompressFree
};

/**
 * Create a new LZ4 compression object.
 */
Converter *lz4CompressNew()
{
    Lz4Compress *this = malloc(sizeof(Lz4Compress));
    *this = (Lz4Compress){.preferences = (LZ4F_preferences_t){.autoFlush=1}};
    return converterNew(this, &lz4CompressIface);
}

/**
 * Prepare to decompress the next compression frame. In general, we have a single frame per file,
 *   but it is possible to have multple frames.
 */
size_t lz4DecompressBegin(Lz4Compress *this, Byte *buf, size_t size, Error *error)
{
    /* LZ4F allocates its own context. We provide a pointer to the pointer which it then updates. */
    size_t result = LZ4F_createDecompressionContext(&this->dctx, LZ4F_VERSION);
    isErrorLz4(result, error);

    return 0;
}


/**
 * Decompress the next block of data. This is the rare case where the decompressed data
 * always fits the output buffer, and the input may be partially procesed.
 * Be sure to decompress any leftover input data.
 */
void lz4DecompressConvert(Lz4Compress *this, Byte *toBuf, size_t *toSize, Byte *fromBuf, size_t *fromSize, Error *error)
{
    /* Convert the uncompressed bytes from the "fromBuf" into the "toBuf". */
    size_t sizeHint = LZ4F_decompress(this->dctx, toBuf, toSize, fromBuf, fromSize, NULL);
    isErrorLz4(sizeHint, error);
}

/**
 * Finish up decompressing a frame. It may flush out data, so be sure to check.
 */
size_t lz4DecompressEnd(Lz4Compress *this, Byte *toBuf, size_t toSize, Error *error)
{
    /* release the compression context and check for errors. */
    isErrorLz4(LZ4F_freeDecompressionContext(this->dctx), error);
    this->dctx = NULL;

    return 0;
}


/**
 * Estimate the decompressed size of a compressed buffer.
 * It is OK to be sloppy, as Lz4DecompressProcess() can handle bad estimates.
 */
size_t lz4DecompressSize(Lz4Filter *this, size_t fromSize)
{
    return 3 * fromSize;  /* Crude estimate, but doesn't need to be precise. */
}


/**
 * Free up resources used for decompression.
 */
void lz4DecompressFree(Lz4Compress *this, Error *error)
{
    /* If not done earlier, release the compression context and check for errors. */
    if (this->cctx != NULL)
        isErrorLz4(LZ4F_freeDecompressionContext(this->dctx), error);

    free(this);
}

ConverterIface lz4DecompressIface =
{
    .fnSize = (ConvertSizeFn)lz4DecompressSize,
    .fnBegin = (ConvertBeginFn)lz4DecompressBegin,
    .fnConvert = (ConvertConvertFn)lz4DecompressConvert,
    .fnEnd = (ConvertEndFn)lz4DecompressEnd,
    .fnFree = (ConvertFreeFn)lz4DecompressFree
};

/**
 * Create a new decompression Converter.
 */
Converter *lz4DecompressNew()
{
    Lz4Compress *this = malloc(sizeof(Lz4Compress));
    *this = (Lz4Compress){0};
    return converterNew(this, &lz4DecompressIface);
}


/**
 * Create a filter for writing and reading compressed files.
 * @param bufferSize - suggested buffer size for efficiency.
 */
Filter *lz4FilterNew(size_t bufferSize, Filter *next)
{
    return convertFilterNew(bufferSize, lz4CompressNew(), lz4DecompressNew(), next);
}
