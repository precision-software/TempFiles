//
// Created by John Morris on 10/12/22.
//

#ifndef FILTER_FILESOURCE_H
#define FILTER_FILESOURCE_H

#include "common/error.h"
#include "common/filter.h"
#include "common/passThrough.h"
#include "common/request.h"

typedef struct FileSource FileSource;

FileSource *fileSourceNew(Filter *filter);
Error fileOpen(FileSource *this, const char *path, int mode, int perm);

size_t fileWrite(FileSource *this, Byte *buf, size_t bufSize, Error *error);
size_t fileRead(FileSource *this, Byte *buf, size_t bufSize, Error *error);

void fileClose(FileSource *this, Error *error);

#endif //FILTER_FILESOURCE_H
