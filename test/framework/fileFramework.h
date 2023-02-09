/* */
#ifndef FILTER_FILEFRAMEWORK_H
#define FILTER_FILEFRAMEWORK_H

#include "iostack.h"


/* Function type to create an IoStack with the given block size */
typedef IoStack *(*CreateStackFn)(size_t blockSize);

void seekTest(CreateStackFn createFn, char *nameFmt);
void singleSeekTest(CreateStackFn createFn, char *nameFmt, size_t fileSize, size_t bufSize);

void streamTest(CreateStackFn createFn, char *nameFmt);
void singleStreamTest(CreateStackFn createFn, char *nameFmt, size_t fileSize, size_t bufSize);

void readSeekTest(CreateStackFn createFn, char *nameFmt);
void singleReadSeekTest(CreateStackFn createFn, char *nameFmt, size_t fileSiae, size_t bufSize);

#include "framework/unitTestInternal.h"


#endif //FILTER_FILEFRAMEWORK_H
