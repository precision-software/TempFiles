/* */
/* Created by John Morris on 11/1/22. */
/* */

#ifndef FILTER_LZ4_H
#define FILTER_LZ4_H
#include "common/filter.h"

typedef struct Lz4Compress Lz4Compress;

Lz4Compress *lz4CompressNew(size_t bufferSize, void *next);
void Lz4CompressFree(void *this);

#endif /*FILTER_LZ4_H */
