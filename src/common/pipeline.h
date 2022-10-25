//
// Created by John Morris on 10/24/22.
//

#ifndef FILTER_PIPELINE_H
#define FILTER_PIPELINE_H

#include "common/filter.h"

Filter *pipelineNew(size_t minRead, size_t maxWrite, ...);

#endif //FILTER_PIPELINE_H
