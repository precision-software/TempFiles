/* */
/* Created by John Morris on 10/12/22. */
/* */

#ifndef FILTER_IoStack_H
#define FILTER_IoStack_H

#include "iostack_error.h"

typedef struct IoStack IoStack;

IoStack *ioStackNew(void *next);

/* The basic requests handled by an I/O Stack */
IoStack *fileOpen(IoStack *this, const char *path, int oflags, int perm, Error *error);
size_t fileWrite(IoStack *this, Byte *buf, size_t bufSize, Error *error);
size_t fileRead(IoStack *this, Byte *buf, size_t bufSize, Error *error);
void fileClose(IoStack *this, Error *error);
off_t fileSeek(IoStack *this, off_t position, Error *error);
void fileDelete(IoStack *this, char *name, Error *error);

/* Helper function for formatted output */
bool filePrintf(void *this, Error *error, char *format, ...);

/* Helper functions to read/write integers in network byte order (big endian) */
bool filePut1(void *this, uint8_t value, Error *error);
bool filePut2(void *this, uint16_t value, Error *error);
bool filePut4(void *this, uint32_t value, Error *error);
bool filePut8(void *this, uint64_t value, Error *error);
uint8_t fileGet1(void *this, Error *error);
uint16_t fileGet2(void *this, Error *error);
uint32_t fileGet4(void *this, Error *error);
uint64_t fileGet8(void *this, Error *error);


#endif /*FILTER_IoStack_H */
