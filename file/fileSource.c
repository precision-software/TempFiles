//
// Created by John Morris on 10/12/22.
//
#include "common/passThrough.h"
#include "fileSource.h"

struct FileSource {
    Filter header;
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
    return passThroughClose(this, error);
}

FileSource *
fileSourceNew(Filter *next)
{
    FileSource *this = malloc(sizeof(FileSource));
    *this = (FileSource) {
        .header = (Filter){.next=next, .iface=&passThroughInterface}
    };

    return this;
}
