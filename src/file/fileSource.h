//
// Created by John Morris on 10/12/22.
//

#ifndef FILTER_FILESOURCE_H
#define FILTER_FILESOURCE_H

#include "common/error.h"
#include "common/filter.h"
#include "common/passThrough.h"

typedef struct FileSource FileSource;

FileSource *fileSourceNew(Filter *filter);
Error fileOpen(FileSource *this, char *path, int mode, int perm);

size_t fileWrite(FileSource *this, Byte *buf, size_t bufSize, Error *error);
size_t fileRead(FileSource *this, Byte *buf, size_t bufSize, Error *error);

void fileClose(FileSource *this, Error *error);

void filePrintf(FileSource *this, Error *error, char *fmt, ...);

void filePut1(FileSource *this, Byte b, Error *error);
void filePut2(FileSource *this, u_int16_t u, Error *error);
void filePut4(FileSource *this, u_int32_t u, Error *error);

#endif //FILTER_FILESOURCE_H
