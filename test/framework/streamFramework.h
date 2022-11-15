//
// Created by John Morris on 10/20/22.
//

#ifndef FILTER_STREAMFRAMEWORK_H
#define FILTER_STREAMFRAMEWORK_H

#include "framework/unitTestInternal.h"

void streamTest(FileSource *pipe, char *nameFmt);
void singleStreamTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

#endif //FILTER_STREAMFRAMEWORK_H
