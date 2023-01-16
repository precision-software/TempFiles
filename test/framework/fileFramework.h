/* */
#ifndef FILTER_FILEFRAMEWORK_H
#define FILTER_FILEFRAMEWORK_H

#include "iostack.h"


void seekTest(IoStack *pipe, char *nameFmt);
void singleSeekTest(IoStack *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

void streamTest(IoStack *pipe, char *nameFmt);
void singleStreamTest(IoStack *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

void readSeekTest(IoStack *pipe, char *nameFmt);
void singleReadSeekTest(IoStack *pipe, char *nameFmt, size_t fileSiae, size_t bufSize);

#include "framework/unitTestInternal.h"

#endif //FILTER_FILEFRAMEWORK_H
