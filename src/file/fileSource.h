/* */
/* Created by John Morris on 10/12/22. */
/* */

#ifndef FILTER_FILESOURCE_H
#define FILTER_FILESOURCE_H

#include "common/error.h"
#include "common/filter.h"
#include "common/passThrough.h"

typedef struct FileSource FileSource;

FileSource *fileSourceNew(void *next);

FileSource *fileOpen(FileSource *this, char *path, int oflags, int perm, Error *error);
size_t fileWrite(FileSource *this, Byte *buf, size_t bufSize, Error *error);
size_t fileRead(FileSource *this, Byte *buf, size_t bufSize, Error *error);
void fileClose(FileSource *this, Error *error);
pos_t fileSeek(FileSource *this, pos_t position, Error *error);
void fileDelete(FileSource *this, char *name, Error *error);

/* Formatted write to a file */
void filePrintf(FileSource *this, Error *error, char *fmt, ...);

/* Some renaming */
#define filePut1 passThroughPut1
#define filePut2 passThroughPut2
#define filePut4 passThroughPut4
#define filePut8 passThroughPut8
#define fileGet1 passThroughGet1
#define fileGet2 passThroughGet2
#define fileGet4 passThroughGet4
#define fileGet8 passThroughGet8

#endif /*FILTER_FILESOURCE_H */
