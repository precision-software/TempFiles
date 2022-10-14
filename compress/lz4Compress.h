//
// Created by John Morris on 10/13/22.
//

#ifndef FILTER_LZ4COMPRESS_H
#define FILTER_LZ4COMPRESS_H

#include <stddef.h>
#include "filter/filter.h"


typedef struct Lz4CompressFilter Lz4CompressFilter;
Filter *lz4CompressFilterNew(void *next, size_t blockSize);


extern FilterInterface lz4CompressInterface;
#endif //FILTER_LZ4COMPRESS_H
