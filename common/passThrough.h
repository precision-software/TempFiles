//
// Created by John Morris on 10/10/22.
//

#ifndef UNTITLED1_PASSTHROUGH_H
#define UNTITLED1_PASSTHROUGH_H
#include "common/filter.h"

extern FilterInterface passThroughInterface;

Error passThroughOpen(void *this, char *path, int mode, int perm);
size_t passThroughRead(void *this, Byte *buf, size_t size, Error *error);
size_t passThroughWrite(void *this, Byte *buf, size_t size, Error *error);
void passThroughClose(void *this, Error *error);
void passThroughAbort(void *this, Error *error);
void passThroughSync(void *this, Error *error);

// Helper function to ensure all the data is written.
size_t passThroughWriteAll(void *this, Byte *buf, size_t size, Error *error);
size_t passThroughReadAll(void *this, Byte *buf, size_t size, Error *error);

#endif //UNTITLED1_PASSTHROUGH_H
