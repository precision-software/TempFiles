/**
 * A Conversion Filter is a general purpose Filter for converting a stream of bytes.
 * The filter does the framework things like opening, closing, reading and writing stream data,
 * and then it invokes a generic "Converter" object to transform data.
 */

#include "common/passThrough.h"
#include "common/error.h"
#include "common/convertFilter.h"
#include "common/converter.h"

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
#include <sys/fcntl.h>


/**
 *
 */
typedef struct ConvertFilter
{
    Filter filter;     /* The abstract Filter, always at front. */
    bool readable;     /* Is the stream opened for reading? */
    bool writeable;    /* Is the stream opened for writing? */
    bool eof;          /* If reading, have we encountered an EOF yet? */

    size_t bufferSize;  /* Extra buffer space if we want it. */
    Buffer *buf;        /* our buffer, big enough for either read or write. */

    Converter *writeConverter;   /* the converter object to use for writing.  (eg. encryption) */
    Converter *readConverter;    /* the converter object to use for reading.  (eg. decryption) */

    Converter *converter;  /* The converter object current being used. */
} ConvertFilter;

/**
 * Estimate the size of the converted data. It is OK to overestimate, but never to underestimate.
 * @param size - the size of the raw data
 * @return - the size of the converted data
 */
size_t convertFilterSize(ConvertFilter *this, size_t writeSize)
{
    /* Save the write size - the nr of before bytes we are moving into our buffer from prev. */
    this->filter.writeSize = converterSize(this->writeConverter, writeSize);

    /* Save the read size - the nr of bytes we request into our buffer from next. */
    size_t readSize = passThroughSize(this, this->filter.writeSize);
    this->filter.readSize = converterSize(this->readConverter, readSize);

    /* Allocate a buffer big enough to hold reads or writes, at least as large as requested. */
    size_t size = sizeRoundUp(this->bufferSize, sizeMax(this->filter.readSize, this->filter.writeSize));
    this->buf = bufferNew(size);

    return this->filter.readSize;
}

/**
 * Open a Filtered Stream.
 * @param path - the path or file name.
 * @param mode - the file mode, say O_RDONLY or O_CREATE.
 * @param perm - if creating a file, the permissions.
 * @return - Error status.
 */
Error convertFilterOpen(ConvertFilter *this, char *path, int mode, int perm)
{
    assert(bufferValid(this->buf));
    this->eof = false;

    /* We support I/O in only one direction per open. */
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    if (this->readable == this->writeable)
        return errorCantBothReadWrite;

    /* Go ahead and open the downstream file. */
    Error error = passThroughOpen(this, path, mode, perm);

    /* Pick the read or the write converter. */
    if (this->readable)
        this->converter = this->readConverter;
    else
        this->converter = this->writeConverter;

    /* Start the converter, keeping in mind it may generate data immediately. */
    size_t actual = converterBegin(this->converter, this->buf->endData, bufferAvailSize(this->buf), &error);
    this->buf->endData += actual;
    assert(bufferValid(this->buf));

    return error;
}

/**
 * Read data from our internal buffer, placing converted data into the caller's buffer.
 */
size_t convertFilterRead(ConvertFilter *this, Byte *buf, size_t size, Error *error)
{
    /* First, check if we have encountered EOF on a previous read. */
    if (this->eof && errorIsOK(*error) )
        *error = errorEOF;
    if (isError(*error))
        return 0;

    /* Try to read downstream data into our buffer if it is empty. */
    bufferFill(this->buf, this, error);

    /* Figure out how much of our input we can convert into the output buffer. */
    /*   Earlier, the Size function told us the size of the input and output data blocks. */
    /*   (by setting this->readSize and prev->readSize) */
    /*   Now we use that info to translate N blocks at a time. */
    assert(size >= this->filter.readSize);
    size_t outSize = size;
    size_t nrBlocks = size / this->filter.readSize;
    size_t inSize = sizeMin(bufferDataSize(this->buf), nrBlocks*this->filter.next->readSize);

    /* Convert that many bytes if we can. We are active as long as some bytes are created or consumed. */
    converterProcess(this->converter, buf, &outSize, this->buf->beginData, &inSize, error);

    /* Update our buffer to reflect the input bytes we just removed. */
    this->buf->beginData += inSize;  if (bufferIsEmpty(this->buf)) bufferReset(this->buf);
    assert(bufferValid(this->buf));

    /* If no bytes were processed, and we had an EOF ... */
    if (outSize == 0 && inSize == 0 && errorIsEOF(*error))
    {
        /* Temporarily ignore the EOF */
        *error = errorOK;
        this->eof = true;

        /* Dump out any trailing data into the output buffer. */
        outSize = converterEnd(this->converter, buf, size, error);

        /* If there isn't any new data, then restore the EOF condition. */
        if (this->eof && outSize == 0)
            *error = errorEOF;
    }

    /* Return the number of bytes read. */
    return outSize;
}

/**
 * Write converted data into our internal buffer, flushing as needed.
 *   @param buf - data to be converted.
 *   @param size - number of bytes to be converted.
 *   @param error - error status, both input and output.
 *   @returns - number of bytes actually used.
 */
size_t convertFilterWrite(ConvertFilter *this, Byte *buf, size_t size, Error *error)
{
    assert(bufferValid(this->buf));

    /* Flush data to assure we have at least a full block of processed space available. */
    if (bufferAvailSize(this->buf) < this->filter.writeSize)
        bufferForceFlush(this->buf, this, error);

    /* Limit the input size to the number of output blocks we can fit into our buffer. */
    /*   The earlier "Size" event helped us determine the size of the input and output blocks. */
    size_t outSize = bufferAvailSize(this->buf);
    size_t nrBlocks = outSize / this->filter.writeSize;
    size_t inSize = sizeMin(size, nrBlocks * this->filter.next->writeSize);

    /* Convert the data. */
    converterProcess(this->converter, this->buf->endData, &outSize, buf, &inSize, error);

    /* Update the buffer to reflect the bytes that were actually added to our internal buf. */
    this->buf->endData+=outSize;
    assert(bufferValid(this->buf));

    return inSize;
}

/**
 * Close this conversion filter, cleaning up and flushing any data not yet output.
 * @param error - error status
 */
void convertFilterClose(ConvertFilter *this, Error *error)
{
    assert(bufferValid(this->buf));
    if (isError(*error)) return;

    /* If we are writing, ... */
    if (this->writeable)
    {
        /* Flush out the destination buffer, so we have room for any trailing data. */
        bufferForceFlush(this->buf, this, error);

        /* Generate the trailing data into the now empty buffer. */
        size_t actual = converterEnd(this->converter, this->buf->endData, bufferAvailSize(this->buf), error);
        this->buf->endData += actual;

        /* Do the final flush of the data just generated. */
        bufferForceFlush(this->buf, this, error);
    }

    /* Notify the downstream objects they must close as well. */
    passThroughClose(this, error);

    /* Make note it is closed. */
    this->readable = this->writeable = false;
    this->converter = NULL;

    assert(bufferValid(this->buf));
}

/**
 * Abstract interface for the conversion filter.
 */
FilterInterface convertInterface = {
        .fnOpen = (FilterOpen) convertFilterOpen,
        .fnRead = (FilterRead) convertFilterRead,
        .fnWrite = (FilterWrite)convertFilterWrite,
        .fnClose = (FilterClose) convertFilterClose,
        .fnSize = (FilterSize) convertFilterSize,
};

/**
 * Create a new conversion filter.
 * @param bufferSize - the suggested size of the data buffers used for conversion.
 * @param writer - the converter used for output (writing).
 * @param reader - the converter used of input (reading)
 * @param next - pointer to the next filter in the pipeline.
 * @return - a new conversion filter.
 */
Filter *convertFilterNew(size_t bufferSize, Converter *writer, Converter* reader, Filter *next)
{
    ConvertFilter *this = malloc(sizeof(ConvertFilter));
    *this = (ConvertFilter) {
            .bufferSize = bufferSize,
            .writeConverter = writer,
            .readConverter = reader
    };

    return filterInit(this, &convertInterface, next);
}

/**
 * Release a conversion filter and its resources.
 * @param error
 */
void conversionFilterFree(ConvertFilter *this, Error *error)
{
    converterFree(this->readConverter, error); this->readConverter = NULL;
    converterFree(this->writeConverter, error); this->writeConverter = NULL;
    free(this);
}
