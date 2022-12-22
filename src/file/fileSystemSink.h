/* */
/* Created by John Morris on 10/11/22. */
/* */

#ifndef UNTITLED1_FILESYSTEMSINK_H
#define UNTITLED1_FILESYSTEMSINK_H

#include "common/filter.h"

typedef struct FileSystemSink FileSystemSink;
FileSystemSink *fileSystemSinkNew(size_t blockSize);

#endif /*UNTITLED1_FILESYSTEMSINK_H */
