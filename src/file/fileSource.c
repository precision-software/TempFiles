/**
 * FileSource is the origin point for file management events.
 * It presents an fread/fwrite style to the entire pipeline.
 * It doesn't do much on its own, as it is
 * more a placeholder for sending events further down the pipeline.
 */
#include "common/passThrough.h"
#include "fileSource.h"

struct FileSource {
    Filter filter;
    bool initialized;
};

/**
 * Open a file, returning error information.
 */
Error fileOpen(FileSource *this, char *path, int mode, int perm)
{
    return passThroughOpen(this, path, mode, perm);
}

/**
 * Write data to a file.
 */
size_t fileWrite(FileSource *this, Byte *buf, size_t bufSize, Error *error)
{
    return passThroughWriteAll(this, buf, bufSize, error);
}


/**
 * Read data from a file.
 */
size_t fileRead(FileSource *this, Byte *buf, size_t bufSize, Error *error)
{
    return passThroughReadAll(this, buf, bufSize, error);
}

void fileSeek(FileSource *this, size_t position, Error *error)
{
    passThroughSeek(this, position, error);
}

/**
 * Close a file.
 */
void fileClose(FileSource *this, Error *error)
{
    if (errorIsEOF(*error))
        *error = errorOK;
    return passThroughClose(this, error);
}


/**
 * Create a new File Source for generating File events. Since this is the
 * first element in a pipeline of filters, it is the handle for the entire pipeline.
 */
FileSource *
fileSourceNew(Filter *next)
{
    FileSource *this = malloc(sizeof(FileSource));
    filterInit(this, &passThroughInterface, next);

    this->filter.writeSize = 16*1024;   /* Suggest size for efficiency on writes. Having a buffer below us makes this unnecessary. */
    this->filter.readSize = passThroughSize(this, 16*1024);

    return this;
}
