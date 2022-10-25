//
// Created by John Morris on 10/12/22.
//
#include "common/passThrough.h"
#include "fileSource.h"

struct FileSource {
    Filter header;
    bool initialized;
};


Error fileOpen(FileSource *this, char *path, int mode, int perm)
{
    if (!this->initialized)
        passThroughSize(this, 16*1024);
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

    passThroughSize(this, 16*1024); // TODO: ????

    return this;
}
