/* */
/* Created by John Morris on 10/12/22. */
/* */

#ifndef FILTER_IoStack_H
#define FILTER_IoStack_H

#include <stdint.h>
#include "iostack_error.h"

typedef struct IoStack IoStack;

IoStack *ioStackNew(void *next);

/* The basic requests handled by an I/O Stack */
void fileOpen(IoStack *this, const char *path, int oflags, int perm, Error *error);
size_t fileWrite(IoStack *this, const Byte *buf, size_t bufSize, Error *error);
size_t fileRead(IoStack *this, Byte *buf, size_t bufSize, Error *error);
void fileClose(IoStack *this, Error *error);
off_t fileSeek(IoStack *this, off_t position, Error *error);
void fileDelete(IoStack *this, const char *name, Error *error);
IoStack *fileClone(IoStack *this);
void fileFree(IoStack *this);
IoStack *fileClone(IoStack *this);
void fileFree(IoStack *this);

/* Helper function for formatted output */
bool filePrintf(void *this, Error *error, const char *format, ...);

/* Helper functions to read/write integers in network byte order (big endian) */
bool filePut1(void *this, uint8_t value, Error *error);
bool filePut2(void *this, uint16_t value, Error *error);
bool filePut4(void *this, uint32_t value, Error *error);
bool filePut8(void *this, uint64_t value, Error *error);
uint8_t fileGet1(void *this, Error *error);
uint16_t fileGet2(void *this, Error *error);
uint32_t fileGet4(void *this, Error *error);
uint64_t fileGet8(void *this, Error *error);

/* Seek to the end of file */
#define FILE_END_POSITION ((off_t)-1)


#endif /*FILTER_IoStack_H */
