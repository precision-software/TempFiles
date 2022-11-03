/**
 *
 */
#ifndef  Stage_FILESPLIT_H
#define  Stage_FILESPLIT_H

#include <sys/syslimits.h>
#include "common/stage.h"

typedef struct FileSplitFilter FileSplitFilter;
typedef void (*PathGetter) (void *data, char *name, size_t segmentIdx, char path[PATH_MAX]);

Stage *fileSplitFilterNew(size_t segmentSize, PathGetter pathGet, void *pathData, Stage *next);

void formatPath(void *fmt, char *name, size_t segmentIdx, char path[PATH_MAX]);

#endif /*FILTER_FILESPLIT_H */
