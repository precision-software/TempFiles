/* */
/* Created by John Morris on 10/10/22. */
/* */

#ifndef UNTITLED1_BufferFile_H
#define UNTITLED1_BufferFile_H

#include "common/filter.h"

typedef struct Buffered Buffered;
Buffered *bufferedNew(size_t blockSize, void *next);

#endif /*UNTITLED1_ByteStream_H */
