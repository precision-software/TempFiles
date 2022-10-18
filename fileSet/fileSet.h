//
// Created by John Morris on 10/16/22.
//

#ifndef FILTER_FILESET_H
#define FILTER_FILESET_H

#include <sys/syslimits.h>
#include "common/filter.h"

typedef struct FileSetFilter FileSetFilter;
typedef void (*PathGetter) (void *data, char *name, size_t segmentIdx, char path[PATH_MAX]);

Filter *fileSetFilterNew(Filter *next, size_t segmentSize, PathGetter pathGet, void *pathData);


void formatPath(void *fmt, char *name, size_t segmentIdx, char path[PATH_MAX]);

#endif //FILTER_FILESET_H
