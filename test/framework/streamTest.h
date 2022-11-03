//
// Created by John Morris on 10/20/22.
//

#ifndef FILTER_STREAMTEST_H
#define FILTER_STREAMTEST_H

#include "framework/unitTestInternal.h"

void streamTest(FileSource *pipe, char *nameFmt);
void singleStreamTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

#endif //FILTER_STREAMTEST_H
