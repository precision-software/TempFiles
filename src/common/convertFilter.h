/* */
/* Created by John Morris on 10/18/22. */
/* */

#ifndef FILTER_CONVERTFILTER_H
#define FILTER_CONVERTFILTER_H

#include "common/filter.h"
#include "common/converter.h"

Filter *convertFilterNew(size_t blockSize, Converter *writer, Converter* reader, Filter *next);

#endif /*FILTER_CONVERTFILTER_H */
