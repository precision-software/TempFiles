/* */
/* Created by John Morris on 10/10/22. */
/* */

#ifndef UNTITLED1_BufferFile_H
#define UNTITLED1_BufferFile_H

#include "common/filter.h"

typedef struct BufferSeek BufferSeek;
Filter *bufferSeekNew(size_t blockSize, Filter *next);

#endif /*UNTITLED1_BufferStream_H */
