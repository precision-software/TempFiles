/* */
#ifndef FILTER_SEEKFRAMEWORK_H
#define FILTER_SEEKFRAMEWORK_H

#include "framework/unitTestInternal.h"

void seekTest(FileSource *pipe, char *nameFmt);
void singleSeekTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

#endif //FILTER_SEEKFRAMEWORK_H
