//
// Created by John Morris on 10/13/22.
//

#ifndef FILTER_LZ4DECOMPRESS_H
#define FILTER_LZ4DECOMPRESS_H

#include <stddef.h>
#include "filter/filter.h"

typedef struct Lz4DecompressFilter Lz4DecompressFilter;
Filter *lz4DecompressFilterNew(Filter *next, size_t blockSize);

extern FilterInterface lz4DecompressInterface;

#endif //FILTER_LZ4DECOMPRESS_H
