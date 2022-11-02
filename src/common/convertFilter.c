//
// Created by John Morris on 10/18/22.
//

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



typedef struct ConvertFilter
{
    Filter filter;
    bool readable;
    bool writeable;
    bool eof;

    size_t bufferSize;  // big enough to hold max compressed block size
    Buffer *buf;        // our buffer.


    Converter *writeConverter;
    Converter *readConverter;

    Converter *converter;
} ConvertFilter;

size_t convertFilterSize(ConvertFilter *this, size_t size)
{
    this->filter.writeSize = size;
    this->filter.readSize = passThroughSize(this, converterSize(this->writeConverter, size));
    return converterSize(this->readConverter, this->filter.readSize);
}
Error convertFilterOpen(ConvertFilter *this, char *path, int mode, int perm)
{
    assert(bufferValid(this->buf));
    this->eof = false;

    // We support I/O in only one direction per open.
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    if (this->readable == this->writeable)
        return errorCantBothReadWrite;


    // Go ahead and open the downstream file.
    Error error = passThroughOpen(this, path, mode, perm);

    // If reading,
    if (this->readable)
        this->converter = this->readConverter;
    else
        this->converter = this->writeConverter;


    // Start the converter
    converterBegin(this->converter, &error);
    assert(bufferValid(this->buf));

    return error;
}

/* Read data from our internal buffer, placing converted data into the caller's buffer.*/
size_t convertFilterRead(ConvertFilter *this, Byte *buf, size_t size, Error *error)
{
    // First, check if we have encountered EOF on a previous read.
    if (this->eof && errorIsOK(*error) )
        *error = errorEOF;
    if (isError(*error))
        return 0;

    // Read downstream data into our buffer if there is room for a full block.
    if (bufferAvailSize(this->buf) >= this->filter.readSize) {
        size_t actual = passThroughRead(this, this->buf->endData, bufferAvailSize(this->buf), error);
        this->buf->endData += actual;
    }

    // Throttle input size to the number of blocks our caller can receive. There is always at least one block requested.
    assert(size >= this->filter.prev->readSize);  // Reads must always request at least one block.
    size_t nrBlocks = size / this->filter.prev->readSize;  // From assertion, nrBlocks > 0.
    size_t inSize = sizeMin(bufferDataSize(this->buf), nrBlocks*this->filter.readSize);
    size_t outSize = size;

    // CASE: internal buffer has data to convert, so convert multiple blocks into the callers buffer.
    if (inSize > 0)
        converterProcess(this->converter, buf, &outSize, this->buf->beginData, &inSize, error);

    // CASE: buffer has no data. Note this corresponds to an EOF since we just tried to fill it.
    else
    {
        // Temporarily ignore the EGF
        this->eof = errorIsEOF(*error);
        if (this->eof)
            *error = errorOK;

        // Dump out any internal data into the output buffer.
        outSize = converterEnd(this->converter, buf, outSize, error);

        // If there isn't any new data, then restore the EOF condition.
        if (outSize == 0 && this->eof && errorIsOK(*error))
            *error = errorEOF;
    }

    // Update the buffer to reflect the bytes that were actually copied out of our internal buffer.
    this->buf->beginData+=inSize; if (bufferIsEmpty(this->buf)) bufferReset(this->buf);
    assert(bufferValid(this->buf));
    return outSize;
}

/* Write converted data into our internal buffer, flushing as needed. */
size_t convertFilterWrite(ConvertFilter *this, Byte *buf, size_t size, Error *error)
{
    assert(bufferValid(this->buf));

    // Flush data to assure we have at least a full block of processed space available.
    if (bufferAvailSize(this->buf) < this->filter.writeSize)
        bufferForceFlush(this->buf, this, error);

    // Limit the input size to the number of output blocks we can fit into our buffer.
    size_t outSize = bufferAvailSize(this->buf);
    size_t nrBlocks = outSize / this->filter.writeSize;
    size_t inSize = sizeMin(size, nrBlocks * this->filter.prev->writeSize);

    // Convert the data.
    size_t temp = outSize;
    converterProcess(this->converter, this->buf->endData, &outSize, buf, &inSize, error);
    assert(outSize <= temp);

    // Update the buffer to reflect the bytes that were actually added to our internal buf.
    this->buf->endData+=outSize;

    assert(bufferValid(this->buf));

    return inSize;
}

void convertFilterClose(ConvertFilter *this, Error *error)
{
    assert(bufferValid(this->buf));
    if (isError(*error)) return;

    // If we are writing, ...
    if (this->writeable)
    {
        // Flush out the destination buffer, so we have room for any trailing data.
        bufferForceFlush(this->buf, this, error);

        // Generate the trailing data into the now empty buffer.
        size_t actual = converterEnd(this->converter, this->buf->endData, bufferAvailSize(this->buf), error);
        this->buf->endData += actual;

        // Do the final flush of the data just generated.
        bufferForceFlush(this->buf, this, error);
    }

    this->readable = this->writeable = false;
    this->converter = NULL;
    passThroughClose(this, error);
    assert(bufferValid(this->buf));
}


FilterInterface convertInterface = {
        .fnOpen = (FilterOpen) convertFilterOpen,
        .fnRead = (FilterRead) convertFilterRead,
        .fnWrite = (FilterWrite)convertFilterWrite,
        .fnClose = (FilterClose) convertFilterClose,
        .fnSize = (FilterSize) convertFilterSize,
};

Filter *convertFilterNew(size_t bufferSize, Converter *writer, Converter* reader, Filter *next)
{
    ConvertFilter *this = malloc(sizeof(ConvertFilter));
    *this = (ConvertFilter) {
            .bufferSize = bufferSize,
            .writeConverter = writer,
            .readConverter = reader,
            .buf = bufferNew(bufferSize)
    };

    return filterInit(this, &convertInterface, next);
}


void conversionFilterFree(ConvertFilter *this, Error *error)
{
    converterFree(this->readConverter, error); this->readConverter = NULL;
    converterFree(this->writeConverter, error); this->writeConverter = NULL;

}
