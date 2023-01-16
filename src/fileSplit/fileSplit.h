/* */
/* Created by John Morris on 10/16/22. */
/* */

#ifndef FILTER_FILESPLIT_H
#define FILTER_FILESPLIT_H

#include <sys/syslimits.h>
#include "common/filter.h"

typedef struct FileSplit FileSplit;

typedef void (*PathGetter) (void *data, const char *name, size_t segmentIdx, char path[PATH_MAX]);

FileSplit *fileSplitNew(size_t segmentSize, PathGetter pathGet, void *pathData, void *next);


void formatPath(void *fmt, const char *name, size_t segmentIdx, char path[PATH_MAX]);

#endif /*FILTER_FILESPLIT_H */
