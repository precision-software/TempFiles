/* */
#ifndef FILTER_FILEFRAMEWORK_H
#define FILTER_FILEFRAMEWORK_H

#include "file/fileSource.h"


void seekTest(FileSource *pipe, char *nameFmt);
void singleSeekTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

void streamTest(FileSource *pipe, char *nameFmt);
void singleStreamTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufSize);

void readSeekTest(FileSource *pipe, char *nameFmt);
void singleReadSeekTest(FileSource *pipe, char *nameFmt, size_t fileSiae, size_t bufSize);

#include "framework/unitTestInternal.h"

#endif //FILTER_FILEFRAMEWORK_H
