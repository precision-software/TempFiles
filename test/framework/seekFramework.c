//
// Created by John Morris on 10/20/22.
//
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/buffered.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/seekFramework.h"
#include "framework/unitTestInternal.h"

void seekTest(FileSource *pipe, char *nameFmt);

/* Given the position in the seek, generate one byte of data for that position. */
static inline Byte generateByte(size_t position)
{
    static char data[] = "The cat in the hat jumped over the quick brown fox while the dog ran away with the spoon.\n";
    size_t idx = position % (sizeof(data)-1);    // Skip the nil character.
    return data[idx];
}

/* Fill a buffer with data appropriate to that position in the seek */
void generateBuffer(size_t position, Byte *buf, size_t size)
{
    for (size_t i = 0; i < size; i++)
        buf[i] = generateByte(position+i);
}

/* Verify a buffer has appropriate data for that position in the test file. */
bool verifyBuffer(size_t position, Byte *buf, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        Byte expected = generateByte(position + i);
        PG_ASSERT_EQ(expected, buf[i]);
    }
    return true;
}

/*
 * Create a file and fill it with known data.
 * The file contains the same line of text repeated over and over, which
 *   - makes it easy to verify with a text editor,
 *   - doesn't align with typical block sizes, and
 *   - is compressible.
 */
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
    PG_ASSERT_OK(error);
    Byte *buf = malloc(bufferSize);

    for (size_t actual, position = 0; position < fileSize; position += actual)
    {
        size_t expected = sizeMin(bufferSize, fileSize-position);
        actual = fileRead(pipe, buf, bufferSize, &error);
        PG_ASSERT_OK(error);
        PG_ASSERT_EQ(expected, actual);
        PG_ASSERT(verifyBuffer(position, buf, actual));
    }

    // Read a final EOF.
    fileRead(pipe, buf, bufferSize, &error);
    PG_ASSERT_EOF(error);

    fileClose(pipe, &error);
    PG_ASSERT_OK(error);
}

/*
 * Create a file and fill it with known data using random seeks.
 * The file contains the same line of text repeated over and over, which
 *   - makes it easy to verify output with a text editor,
 *   - doesn't align with typical block sizes, and
 *   - is compressible.
 */
void allocateFile(FileSource *pipe, char *path, size_t fileSize, size_t bufferSize)
{
    /* Start out by allocating space and filling the file with "X"s. */
    Error error = fileOpen(pipe, path, O_WRONLY|O_CREAT|O_TRUNC, 0);
    PG_ASSERT_OK(error);
    Byte *buf = malloc(bufferSize);
    memset(buf, 'X', bufferSize);

    size_t position;
    for (position = 0; position < fileSize; position += bufferSize)
    {
        size_t expected = sizeMin(bufferSize, fileSize-position);
        size_t actual = fileWrite(pipe, buf, expected, &error);
        PG_ASSERT_OK(error);
        PG_ASSERT_EQ(actual, expected);
    }

    fileClose(pipe, &error);
    free(buf);

    PG_ASSERT_OK(error);
}

static const int prime = 3197;

void generateRandomFile(FileSource *pipe, char *path, size_t fileSize, size_t blockSize)
{
    /* The nr of blocks must be relatively prime to "prime", otherwise we won't visit all the blocks. */
    size_t nrBlocks = (fileSize + blockSize - 1) / blockSize;
    PG_ASSERT( nrBlocks == 0 || (nrBlocks % prime) != 0);

    Error error = fileOpen(pipe, path, O_RDWR, 0);
    PG_ASSERT_OK(error);
    Byte *buf = malloc(blockSize);


    for (size_t idx = 0; idx < nrBlocks; idx++)
    {
        /* Pick a pseudo-random block and seek to it */
        size_t position = ((idx * prime) % nrBlocks) * blockSize;
        //printf("fileSeek - idx = %u  recordNr=%u nrBlocks=%u\n", idx, (idx*prime)%nrBlocks, nrBlocks);
        fileSeek(pipe, position, &error);
        PG_ASSERT_OK(error);

        /* Generate data appropriate for that block. */
        size_t expected = sizeMin(blockSize, fileSize - position);
        generateBuffer(position, buf, expected);

        /* Write the block */
        size_t actual = fileWrite(pipe, buf, expected, &error);
        PG_ASSERT_OK(error);
        PG_ASSERT_EQ(actual, expected);
    }

    fileClose(pipe, &error);
    PG_ASSERT_OK(error);
}

void appendFile(FileSource *pipe, char *path, size_t fileSize, size_t blockSize)
{
    Error error = fileOpen(pipe, path, O_RDWR, 0);
    PG_ASSERT_OK(error);
    Byte *buf = malloc(blockSize);

    /* Seek to the end of the file */
    pos_t endPosition = fileSeek(pipe, FILE_END_POSITION, &error);
    PG_ASSERT_OK(error);
    PG_ASSERT_EQ(fileSize, endPosition);

    /* Write a new block at the end of file */
    generateBuffer(endPosition, buf, blockSize);

    /* Write the block */
    size_t actual = fileWrite(pipe, buf, blockSize, &error);
    PG_ASSERT_OK(error);
    PG_ASSERT_EQ(actual, blockSize);

    /* Close the file and verify it is correct. */
    fileClose(pipe, &error);
    PG_ASSERT_OK(error);

    verifyFile(pipe, path, fileSize+blockSize, blockSize);
}

/*
 * Verify a file has the correct data through randomlike seeks.
 * This should do a complete verification - examining every byte of the file.
 */
void verifyRandomFile(FileSource *pipe, char *path, size_t fileSize, size_t blockSize)
{
    Error error = fileOpen(pipe, path, O_RDONLY, 0);
    PG_ASSERT_OK(error);
    Byte *buf = malloc(blockSize);

    size_t nrBlocks = (fileSize + blockSize -1) / blockSize;
    PG_ASSERT(nrBlocks == 0 || (nrBlocks % prime) != 0);
    for (size_t idx = 0;  idx < nrBlocks; idx++)
    {
        /* Pick a pseudo-random block and read it */
        size_t position = ((idx * prime) % nrBlocks) * blockSize;
        fileSeek(pipe, position, &error);
        PG_ASSERT_OK(error);
        size_t actual = fileRead(pipe, buf, blockSize, &error);
        PG_ASSERT_OK(error);

        /* Verify we read the correct data */
        size_t expected = sizeMin(blockSize, fileSize-position);
        PG_ASSERT_EQ(actual, expected);
        PG_ASSERT(verifyBuffer(position, buf, actual));
    }

    fileClose(pipe, &error);
    PG_ASSERT_OK(error);
}


/* Run a test on a single configuration determined by file size and buffer size */
void singleSeekTest(FileSource *pipe, char *nameFmt, size_t fileSize, size_t bufferSize)
{
    char fileName[PATH_MAX];
    snprintf(fileName, sizeof(fileName), nameFmt, fileSize, bufferSize);
    beginTest(fileName);

    allocateFile(pipe, fileName, fileSize, bufferSize);
    generateRandomFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);
    verifyRandomFile(pipe, fileName, fileSize, bufferSize);
}

/* run a matrix of tests for various file sizes and I/O sizes.  All will use a 1K block size. */
void seekTest(FileSource *pipe, char *nameFmt)
{
    size_t fileSize[] = {1024, 0, 64, 1027, 1, 1024*1024, 64*1024*1024 + 127};
    size_t bufSize[] = {1024, 32*1024, 64, 35, 2037, 1};
#define countof(array) (sizeof(array)/sizeof(array[0]))

    for (int fileIdx = 0; fileIdx<countof(fileSize); fileIdx++)
        for (int bufIdx = 0; bufIdx<countof(bufSize); bufIdx++)
            if  (fileSize[fileIdx] / bufSize[bufIdx] < 4*1024*1024)  // Keep nr blocks under 4M to complete in reasonable time.
                singleSeekTest(pipe, nameFmt, fileSize[fileIdx], bufSize[bufIdx]);
}
