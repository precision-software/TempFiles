/* */
/* Created by John Morris on 10/12/22. */
/* */

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
pos_t fileSeek(FileSource *this, pos_t position, Error *error);

/* Formatted write to a file */
void filePrintf(FileSource *this, Error *error, char *fmt, ...);

/*
 * Note: may want to optimize (inline) so we talk directly to blockify's buffer.
 * In the meantime, the following routines will do the job generically.
 */

/* Write values in network byte order (big endian) */
void filePut1(FileSource *this, Byte b, Error *error);
void filePut2(FileSource *this, u_int16_t u, Error *error);
void filePut4(FileSource *this, u_int32_t u, Error *error);
void filePut8(FileSource *this, u_int64_t, Error *error);

/* Read values in network byte order */
uint8_t fileGet1(FileSource *this, Error *error);
uint16_t fileGet2(FileSource *this, Error *error);
uint32_t fileGet4(FileSource *this, Error *error);
uint32_t fileGet8(FileSource *this, Error *error);

#endif /*FILTER_FILESOURCE_H */
