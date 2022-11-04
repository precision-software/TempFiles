/* */
/* Created by John Morris on 10/16/22. */
/* */

#ifndef FILTER_FILESPLIT_H
#define FILTER_FILESPLIT_H

#include <sys/syslimits.h>
#include "common/filter.h"


typedef void (*PathGetter) (void *data, char *name, size_t segmentIdx, char path[PATH_MAX]);

Filter *fileSplitFilterNew(size_t segmentSize, PathGetter pathGet, void *pathData, Filter *next);


void formatPath(void *fmt, char *name, size_t segmentIdx, char path[PATH_MAX]);

#endif /*FILTER_FILESPLIT_H */
