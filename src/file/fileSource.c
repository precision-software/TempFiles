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
FileSource *fileOpen(FileSource *this, char *path, int oflags, int perm, Error *error)
{
    /* Appending to a file is tricky for encrypted/compressed files. */
    bool append = (oflags & O_APPEND) != 0;
    oflags &= (~O_APPEND);

    /* Open the file */
    Filter *next = passThroughOpen(this, path, oflags, perm, error);
    FileSource *new = fileSourceNew(next);

    /* Negotiate record sizes */
    if (errorIsOK(*error))
        passThroughBlockSize(new, this->blockSize, error);

    /* If we are appending, then seek to the end. */
    if (append)
        fileSeek(new, FILE_END_POSITION, error);

    ;
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


/*
 * Seek to the last partial record in the file, or EOF if all records
 * are full sized. (Think of EOF as a final, empty record.)
 */
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
    passThroughClose(this, error);
    free(this);
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

    this->blockSize = 1;   /* We do not conform to record boundaries. */

    return this;
}
