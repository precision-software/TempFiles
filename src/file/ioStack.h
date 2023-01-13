/* */
/* Created by John Morris on 10/12/22. */
/* */

#ifndef FILTER_IoStack_H
#define FILTER_IoStack_H

#include "common/error.h"
#include "common/filter.h"
#include "common/passThrough.h"

typedef struct IoStack IoStack;

IoStack *ioStackNew(void *next);

IoStack *fileOpen(IoStack *this, char *path, int oflags, int perm, Error *error);
size_t fileWrite(IoStack *this, Byte *buf, size_t bufSize, Error *error);
size_t fileRead(IoStack *this, Byte *buf, size_t bufSize, Error *error);
void fileClose(IoStack *this, Error *error);
pos_t fileSeek(IoStack *this, pos_t position, Error *error);
void fileDelete(IoStack *this, char *name, Error *error);

/* Formatted write to a file */
void filePrintf(IoStack *this, Error *error, char *fmt, ...);

/* Some renaming */
#define filePut1 passThroughPut1
#define filePut2 passThroughPut2
#define filePut4 passThroughPut4
#define filePut8 passThroughPut8
#define fileGet1 passThroughGet1
#define fileGet2 passThroughGet2
#define fileGet4 passThroughGet4
#define fileGet8 passThroughGet8

#endif /*FILTER_IoStack_H */
