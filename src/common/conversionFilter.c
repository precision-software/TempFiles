//
// Created by John Morris on 10/18/22.
//

#include "common/passThrough.h"
#include "common/error.h"
#include "common/conversionFilter.h"

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
struct ConversionFilter
{
    Filter header;
    bool readable;
    bool writeable;
    size_t blockSize;   // uncompressed block size
    size_t bufferSize;  // big enough to hold max compressed block size
    Buffer *buf;        // our buffer.
    void *converterConfig;
    Converter *(*newToConverter)(void *config, Error *error);
    Converter *(*newFromConverter)(void *config, Error *error);
    Converter *converter;
};

Error conversionFilterOpen(ConversionFilter *this, char *path, int mode, int perm)
{

    // We support I/O in only one direction per open.
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    if (this->readable == this->writeable)
        return errorCantBothReadWrite;

    // Go ahead an open the file downstream.
    Error error = passThroughOpen(this, path, mode, perm);

    // Are converting To an output file, or converting from an input file?
    if (this->writeable)
        this->converter = this->newToConverter(this->converterConfig, &error);
    else
        this->converter = this->newFromConverter(this->converterConfig, &error);

    return error;
}

size_t conversionFilterRead(ConversionFilter *this, Byte *buf, size_t size, Error *error)
{
    size_t outSize = 0, inSize = 0;
    bufferFill(this->buf, this);

    converterConvert(this->converter, this->buf->writePtr, &inSize, buf, &outSize, error);
    this->buf->readPtr+=outSize; if (bufferIsEmpty(this->buf)) bufferReset(this->buf);

    return outSize;
}

Error errorNotImplemented = (Error){.code=errorCodeFilter, .msg="Not Implemented"};

size_t conversionFilterWrite(ConversionFilter *this, Byte *buf, size_t size, Error *error)
{
    *error = errorNotImplemented;
    return 0;
}

void conversionFilterClose(ConversionFilter *this, Error *error)
{
    if (isError(*error)) return;
    *error = bufferForceFlush(this->buf, this);  // TODO: only want one final, partial write??  Use error as a param.
    converterEndFrame(this->converter, error);
    if (isError(*error)) return;
    bufferForceFlush(this->buf, this);

    converterFree(this->converter, error);
    this->converter = NULL;

    passThroughClose(this, error);
}


FilterInterface genericInterface = {
        .fnOpen = (FilterOpen)conversionFilterOpen,
        .fnRead = (FilterRead)conversionFilterRead,
        .fnWrite = (FilterWrite)conversionFilterWrite,
        .fnClose = (FilterClose)conversionFilterClose,
};


Filter *conversionFilterNew(Filter *next, size_t blockSize, Converter *(*newTo)(void *), Converter *(*newFrom)(void *))
{
    ConversionFilter *this = malloc(sizeof(ConversionFilter));
    *this = (ConversionFilter) {
            .header = (Filter) {
                    .blockSize = blockSize,
                    .iface = &genericInterface,
                    .next = next,
            },
            .blockSize = blockSize,
    };

    return (Filter *)this;
}


void conversionFilterFree(ConversionFilter *this)
{

}
