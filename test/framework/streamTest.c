//
// Created by John Morris on 10/20/22.
//
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/streamTest.h"
#include "framework/unitTest.h"

void streamTest(FileSource *pipe, char *nameFmt);

/* Given the position in the stream, generate one byte of data for that position. */
static inline Byte generateByte(size_t position)
{
    static char data[] = "The cat in the hat jumped over the quick brown fox while the dog ran away with the spoon.";
    size_t idx = position % sizeof(data);
    return data[idx];
}

/* Fill a buffer with data appropriate to that position in the stream */
void generateBuffer(size_t position, Byte *buf, size_t size)
{
    for (size_t i = 0; i < size; i++)
        buf[i] = generateByte(position+i);
}

/* Verify a buffer has appropriate data for that position in the stream */
bool verifyBuffer(size_t position, Byte *buf, size_t size)
{
    for (size_t i = 0; i < size; i++)
        if (buf[i] != generateByte(position+i))
            return false;
    return true;
}

/* Create a file and fill it with known data */
void generateFile(FileSource *pipe, char *path, size_t fileSize, size_t bufferSize)
{
    Error error = fileOpen(pipe, path, O_WRONLY|O_CREAT|O_TRUNC, 0);
    Byte *buf = malloc(bufferSize);

    size_t position;
    for (position = 0; position < fileSize; position += bufferSize)
    {
        size_t expected = sizeMin(bufferSize, fileSize-position);
        generateBuffer(position, buf, expected);
        size_t actual = fileWrite(pipe, buf, expected, &error);
        PG_ASSERT_OK(error);
        PG_ASSERT_EQ(actual, expected);
    }

    free(buf);
    fileClose(pipe, &error);
    PG_ASSERT_OK(error);
}

/* Verify a file has the correct data */
void verifyFile(FileSource *pipe, char *path, size_t fileSize, size_t bufferSize)
{
    Error error = fileOpen(pipe, path, O_RDONLY, 0);
    bufferSize = sizeMax(bufferSize, ((Filter*)pipe)->readSize);
    Byte *buf = malloc(bufferSize);

    size_t position;
    size_t actual;
    for (position = 0; position < fileSize; position += actual)
    {
        size_t expected = sizeMin(bufferSize, fileSize-position);
        actual = fileRead(pipe, buf, bufferSize, &error);
        PG_ASSERT_OK(error);
        PG_ASSERT(verifyBuffer(position, buf, actual));
    }

    // Read a final EOF.
    fileRead(pipe, buf, bufferSize, &error);
    PG_ASSERT_EOF(error);

    fileClose(pipe, &error);
    PG_ASSERT_OK(error);
}

/* Run a test on a single configuration determined by file size and buffer size */
void singleStreamTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufferSize)
{
    char fileName[PATH_MAX];
    snprintf(fileName, sizeof(fileName), nameFmt, fileSize, bufferSize);
    beginTest(fileName);
    generateFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);
}

/* run a matrix of tests for various file sizes and buffer sizes */
void streamTest(FileSource *pipe, char *nameFmt)
{
    size_t fileSize[] = {1024, 0, 64, 1027, 1, 1024*1024, 64*1024*1024 + 127};
    size_t bufSize[] = {1024, 32*1024, 64, 1};
#define countof(array) (sizeof(array)/sizeof(array[0]))

    for (int fileIdx = 0; fileIdx<countof(fileSize); fileIdx++)
        for (int bufIdx = 0; bufIdx<countof(bufSize); bufIdx++)
            singleStreamTest(pipe, nameFmt, fileSize[fileIdx], bufSize[bufIdx]);
}
