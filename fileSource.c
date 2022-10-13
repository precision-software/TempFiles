//
// Created by John Morris on 10/12/22.
//
#include "passThrough.h"
#include "fileSource.h"


typedef struct FileSource {
    Filter header;
} FileSource;


Error fileOpen(FileSource *this, const char *path, int mode, int perm)
{
    OpenRequest req = (OpenRequest) {.path=path, .mode=mode, .perm=perm};
    passThroughOpen(this, &req);
    return req.error;
}

size_t fileWrite(FileSource *this, Byte *buf, size_t bufSize, Error *error)
{
    if (!errorIsOK(*error))
        return 0;

    WriteRequest req = (WriteRequest) {.buf=buf, .bufSize=bufSize};
    passThroughWriteAll(this, &req);
    *error = req.error;

    if (!errorIsOK(*error))
        return 0;

    return req.actualSize;
}

size_t fileRead(FileSource *this, Byte *buf, size_t bufSize, Error *error)
{
    if (!errorIsOK(*error))
        return 0;

    ReadRequest req = (ReadRequest) {.buf=buf, .bufSize=bufSize};
    passThroughReadAll(this, &req);
    *error = req.error;

    if (!errorIsOK(*error))
        return 0;

    return req.actualSize;
}

void fileClose(FileSource *this, Error *error)
{
    CloseRequest req = (CloseRequest){};
    passThroughClose(this, &req);
    if (errorIsOK(*error))
        *error = req.error;
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
