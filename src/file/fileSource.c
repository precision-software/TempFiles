/**
 * FileSource is the origin point for file management events.
 * It presents an fread/fwrite style to the entire pipeline.
 * It doesn't do much on its own, as it is
 * more a placeholder for sending events further down the pipeline.
 */
#include <stdlib.h>
#include <sys/fcntl.h>
#include "common/passThrough.h"
#include "fileSource.h"

struct FileSource {
    Filter filter;
    size_t blockSize;
};

/**
 * Open a file, returning error information.
 */
Error fileOpen(FileSource *this, char *path, int mode, int perm)
{
    /* Appending to a file is tricky for encrypted/compressed files. */
    bool append = (mode & O_APPEND) != 0;
    mode &= (~O_APPEND);

    /* Open the file */
    Error error = passThroughOpen(this, path, mode, perm);
    if (errorIsOK(error))
        passThroughBlockSize(this, this->blockSize, &error);

    /* If we are appending, then seek to the end of the file */
    if (append)
        fileSeek(this, FILE_END_POSITION, &error);  /* TODO: EOF or last block? */

    return error;
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
size_t fileRead(FileSource *this, Byte *buf, size_t size, Error *error)
{
    return passThroughReadAll(this, buf, size, error);
}

pos_t fileSeek(FileSource *this, pos_t position, Error *error)
{
    return passThroughSeek(this, position, error);
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

    this->blockSize = 16*1024;   /* Suggest size for efficiency on writes. Having a buffer below us makes this unnecessary. */

    return this;
}
