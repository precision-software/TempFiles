/* */
/* Created by John Morris on 10/15/22. */
/* */

#ifndef FILTER_LZ4_H
#define FILTER_LZ4_H
#include "common/filter.h"

typedef struct Lz4Filter Lz4Filter;

Filter *lz4FilterNew(size_t bufferSize, Filter *next);
void Lz4FilterFree(void *this);

#endif /*FILTER_LZ4_H */
