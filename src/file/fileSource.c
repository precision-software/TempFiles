//
// Created by John Morris on 10/12/22.
//
#include "common/passThrough.h"
#include "fileSource.h"

struct FileSource {
    Filter filter;
    bool initialized;
};


Error fileOpen(FileSource *this, char *path, int mode, int perm)
{
    return passThroughOpen(this, path, mode, perm);
}

size_t fileWrite(FileSource *this, Byte *buf, size_t bufSize, Error *error)
{
    return passThroughWriteAll(this, buf, bufSize, error);
}

size_t fileRead(FileSource *this, Byte *buf, size_t bufSize, Error *error)
{
    return passThroughReadAll(this, buf, bufSize, error);
}

void fileClose(FileSource *this, Error *error)
{
    if (errorIsEOF(*error))
        *error = errorOK;
    return passThroughClose(this, error);
}


FileSource *
fileSourceNew(Filter *next)
{
    FileSource *this = malloc(sizeof(FileSource));
    filterInit(this, &passThroughInterface, next);

    this->filter.writeSize = 16*1024;   // Suggest size for efficiency on writes. Having a buffer below us makes this unnecessary.
    this->filter.readSize = passThroughSize(this, 16*1024);

    return this;
}
