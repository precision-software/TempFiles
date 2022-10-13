//
// Created by John Morris on 10/10/22.
//

#ifndef UNTITLED1_BUFFERFILTER_H
#define UNTITLED1_BUFFERFILTER_H

#include "filter.h"

typedef struct BufferFilter BufferFilter;
Filter *bufferFilterNew(Filter *next);

#endif //UNTITLED1_BUFFERFILTER_H
