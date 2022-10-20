/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "unitTest.h"
#define TEST_DIR "/tmp/pgtest/"

void streamTest(FileSource *stream, char *nameFmt);


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");
    system("rm -rf " TEST_DIR "compressed; mkdir -p " TEST_DIR "compressed");
    system("rm -rf " TEST_DIR "split; mkdir -p " TEST_DIR "split");

    beginTestGroup("Buffered Files");
    FileSource *stream = fileSourceNew(bufferStreamNew(fileSystemSinkNew()));
    streamTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.

    beginTestGroup("LZ4 Compression");
    FileSource *lz4 = fileSourceNew(lz4FilterNew(bufferStreamNew(fileSystemSinkNew()), 16 * 1024));
    //streamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4");
    //compressionVerify("LZ4 Compression: verify with external utility");

    beginTestGroup("File Splitting");
    FileSource *split =
            fileSourceNew(
                    bufferStreamNew(
                            fileSplitFilterNew(
                                    fileSystemSinkNew(),
                                    1024 * 1024,
                                    formatPath, "%s-%06d.seg")));
    streamTest(split, TEST_DIR "split/testfile_%u_%u");
    //splitVerify("Split into multiple files: verify files");
}

void streamTest(FileSource *pipe, char *nameFmt);

static inline Byte generateByte(size_t position)
{
    static char data[] = "The cat in the hat jumped over the quick brown fox while the dog ran away with the spoon.";
    size_t idx = position % sizeof(data);
    return data[idx];
}

void generateBuffer(size_t position, Byte *buf, size_t size)
{
    for (size_t i = 0; i < size; i++)
        buf[i] = generateByte(position+i);
}

bool verifyBuffer(size_t position, Byte *buf, size_t size)
{
    for (size_t i = 0; i < size; i++)
        if (buf[i] != generateByte(position+i))
            return false;
    return true;
}


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


void verifyFile(FileSource *pipe, char *path, size_t fileSize, size_t bufferSize)
{
    Error error = fileOpen(pipe, path, O_RDONLY, 0);
    Byte *buf = malloc(sizeMax(1,bufferSize));

    size_t position;
    for (position = 0; position < fileSize; position += bufferSize)
    {
        size_t expected = sizeMin(bufferSize, fileSize-position);
        size_t actual = fileRead(pipe, buf, bufferSize, &error);
        PG_ASSERT_OK(error);
        PG_ASSERT_EQ(actual, expected);
        PG_ASSERT(verifyBuffer(position, buf, expected));
    }

    size_t actual = fileRead(pipe, buf, bufferSize, &error);
    PG_ASSERT_EOF(error);
    PG_ASSERT_EQ(actual, 0);

    fileClose(pipe, &error);
    PG_ASSERT_OK(error);
}

void singleStreamTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufferSize)
{
    char fileName[PATH_MAX];
    snprintf(fileName, sizeof(fileName), nameFmt, fileSize, bufferSize);
    generateFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);
}

void streamTest(FileSource *pipe, char *nameFmt)
{
    size_t fileSize[] = {0, 1, 16, 1024, 1027, 1024*1024, 64*1024*1024 + 127};
    size_t bufSize[] = {1, 16, 1024, 32*1024};
#define countof(array) (sizeof(array)/sizeof(array[0]))

    for (int fileIdx = 0; fileIdx<countof(fileSize); fileIdx++)
        for (int bufIdx = 0; bufIdx<countof(bufSize); bufIdx++)
            singleStreamTest(pipe, nameFmt, fileSize[fileIdx], bufSize[bufIdx]);
}



// TODO: Make it OK to clase an already closed pipeline.
