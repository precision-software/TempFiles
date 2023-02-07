/**/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>

#define DEBUG
#include "../../src/framework/debug.h"
#include "../../src/iostack.h"
#include "framework/fileFramework.h"
#include "framework/unitTestInternal.h"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

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
		if (expected != buf[i])
			debug("verifyBuffer: i=%zu position=%zu  buf[i]=%c expected=%c\n", i, position, buf[i], expected);
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
void generateFile(IoStack *iostack, char *path, size_t fileSize, size_t bufferSize)
{
    debug("generateFile: path=%s\n", path);
    PG_ASSERT(fileOpen(iostack, path, O_WRONLY|O_CREAT|O_TRUNC, 0));
    Byte *buf = malloc(bufferSize); /* TODO: make buf be at end of struct */

    size_t position;
    for (position = 0; position < fileSize; position += bufferSize)
    {
        size_t expected = MIN(bufferSize, fileSize-position);
        generateBuffer(position, buf, expected);
        size_t actual = fileWriteAll(iostack, buf, expected, position, 0);
        PG_ASSERT_EQ(expected, actual);
    }

    free(buf);
    PG_ASSERT(fileClose(iostack));
}

/* Verify a iostack has the correct data */
void verifyFile(IoStack *iostack, char *path, size_t fileSize, size_t bufferSize)
{
    debug("verifyFile: path=%s\n", path);
    PG_ASSERT(fileOpen(iostack, path, O_RDONLY, 0));
    Byte *buf = malloc(bufferSize);

    for (size_t actual, position = 0; position < fileSize; position += actual)
    {
        size_t expected = MIN(bufferSize, fileSize-position);
        actual = fileReadAll(iostack, buf, bufferSize, position, 0);
        PG_ASSERT_EQ(expected, actual);
        PG_ASSERT(verifyBuffer(position, buf, actual));
    }

    // Read a final EOF.
	PG_ASSERT(!fileEof(iostack));
    fileReadAll(iostack, buf, 1, fileSize, 0);
    PG_ASSERT(fileEof(iostack));

    PG_ASSERT(fileClose(iostack));
}

/*
 * Create a file and fill it with known data using random seeks.
 * The file contains the same line of text repeated over and over, which
 *   - makes it easy to verify output with a text editor,
 *   - doesn't align with typical block sizes, and
 *   - is compressible.
 */
void allocateFile(IoStack *file, char *path, size_t fileSize, size_t bufferSize)
{
    debug("allocateFile: path=%s\n", path);
    /* Start out by allocating space and filling the file with "X"s. */
    PG_ASSERT(fileOpen(file, path, O_WRONLY|O_CREAT|O_TRUNC, 0));
    Byte *buf = malloc(bufferSize);
    memset(buf, 'X', bufferSize);

    size_t position;
    for (position = 0; position < fileSize; position += bufferSize)
    {
        size_t expected = MIN(bufferSize, fileSize-position);
        size_t actual = fileWriteAll(file, buf, expected, position, 0);
        PG_ASSERT_EQ(actual, expected);
    }

    PG_ASSERT(fileClose(file));
    free(buf);
}

static const int prime = 3197;

void generateRandomFile(IoStack *file, char *path, size_t fileSize, size_t blockSize)
{
    debug("generateRandomFile: path=%s\n", path);
    /* The nr of blocks must be relatively prime to "prime", otherwise we won't visit all the blocks. */
    size_t nrBlocks = (fileSize + blockSize - 1) / blockSize;
    PG_ASSERT( nrBlocks == 0 || (nrBlocks % prime) != 0);

    PG_ASSERT(fileOpen(file, path, O_RDWR, 0));
    Byte *buf = malloc(blockSize);


    for (size_t idx = 0; idx < nrBlocks; idx++)
    {
        /* Pick a pseudo-random block and seek to it */
        size_t position = ((idx * prime) % nrBlocks) * blockSize;
        //printf("fileSeek - idx = %u  blockNr=%u nrBlocks=%u\n", idx, (idx*prime)%nrBlocks, nrBlocks);

        /* Generate data appropriate for that block. */
        size_t expected = MIN(blockSize, fileSize - position);
        generateBuffer(position, buf, expected);

        /* Write the block */
        size_t actual = fileWriteAll(file, buf, expected, position, 0);
        PG_ASSERT_EQ(actual, expected);
    }

    PG_ASSERT(fileClose(file));
}

void appendFile(IoStack *iostack, char *path, size_t fileSize, size_t blockSize)
{
    debug("appendFile: path=%s\n", path);
    PG_ASSERT(fileOpen(iostack, path, O_RDWR, 0));
    Byte *buf = malloc(blockSize);

    /* Seek to the end of the file - should match file size */
    off_t endPosition = fileSize(iostack);

    /* Write a new block at the end of file */
    generateBuffer(endPosition, buf, blockSize);

    /* Write the block */
    size_t actual = fileWriteAll(iostack, buf, blockSize, endPosition, 0);
    PG_ASSERT_EQ(actual, blockSize);

    /* Close the file and verify it is correct. */
    PG_ASSERT(fileClose(iostack));

    verifyFile(iostack, path, fileSize+blockSize, blockSize);
}

/*
 * Verify a ioStack has the correct data through randomlike seeks.
 * This should do a complete verification - examining every byte of the ioStack.
 */
void verifyRandomFile(IoStack *ioStack, char *path, size_t fileSize, size_t blockSize)
{
    debug("verifyRandomFile: path=%s\n", path);
    PG_ASSERT(fileOpen(ioStack, path, O_RDONLY, 0));
    Byte *buf = malloc(blockSize);

    size_t nrBlocks = (fileSize + blockSize -1) / blockSize;
    PG_ASSERT(nrBlocks == 0 || (nrBlocks % prime) != 0);
    for (size_t idx = 0;  idx < nrBlocks; idx++)
    {
        /* Pick a pseudo-random block and read it */
        size_t position = ((idx * prime) % nrBlocks) * blockSize;

        size_t actual = fileReadAll(ioStack, buf, blockSize, position, 0);

        /* Verify we read the correct data */
        size_t expected = MIN(blockSize, fileSize-position);
        PG_ASSERT_EQ(actual, expected);
        PG_ASSERT(verifyBuffer(position, buf, actual));
    }

    PG_ASSERT(fileClose(ioStack));
}


void deleteFile(IoStack *pipe, char *name)
{
	unlink(name);
}


void openFile(IoStack *pipe, char *name)
{

	PG_ASSERT(!fileOpen(pipe, "BADNAME", O_RDWR, 0));
	PG_ASSERT_EQ(errno, ENOENT);

	PG_ASSERT(!fileOpen(pipe, "BADNAME2", O_RDONLY, 0));
	PG_ASSERT_EQ(errno, ENOENT);

	PG_ASSERT(fileOpen(pipe, "GOODNAME", O_CREAT | O_WRONLY, 0));
	PG_ASSERT(fileClose(pipe));

	PG_ASSERT(fileOpen(pipe, "GOODNAME", O_RDONLY, 0));
	PG_ASSERT(fileClose(pipe));

	/* OK to close an already closed file */
	PG_ASSERT(fileClose(pipe));

	deleteFile(pipe, "GOODNAME");
}




/* Run a test on a single configuration determined by file size and buffer size */
void singleSeekTest(IoStack *pipe, char *nameFmt, size_t fileSize, size_t bufferSize)
{
    char fileName[PATH_MAX];
    snprintf(fileName, sizeof(fileName), nameFmt, fileSize, bufferSize);
    beginTest(fileName);

	openFile(pipe, fileName);

    /* create and read back as a stream */
    generateFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);

    /* Fill in the file with garbage, then write it out as random writes */
    allocateFile(pipe, fileName, fileSize, bufferSize);
    generateRandomFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);

    /* append to the file */
    appendFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize+bufferSize, 16*1024);

    /* Read back as random reads */
    verifyRandomFile(pipe, fileName, fileSize+bufferSize, bufferSize);

    /* Clean things up */
    deleteFile(pipe, fileName);
}

/* run a matrix of tests for various file sizes and I/O sizes.  All will use a 1K block size. */
void seekTest(IoStack *pipe, char *nameFmt)
{
    size_t fileSize[] = {1024, 0, 64, 1027, 1, 1024*1024, 64*1024*1024 + 127};
    size_t bufSize[] = {1024, 32*1024, 64, 35, 2037, 1};
#define countof(array) (sizeof(array)/sizeof(array[0]))

    for (int fileIdx = 0; fileIdx<countof(fileSize); fileIdx++)
        for (int bufIdx = 0; bufIdx<countof(bufSize); bufIdx++)
            if  (fileSize[fileIdx] / bufSize[bufIdx] < 4*1024*1024)  // Keep nr blocks under 4M to complete in reasonable time.
                singleSeekTest(pipe, nameFmt, fileSize[fileIdx], bufSize[bufIdx]);
}



/* Run a test on a single configuration determined by file size and buffer size */
void singleStreamTest(IoStack *pipe, char *nameFmt, size_t fileSize, size_t bufferSize)
{
    char fileName[PATH_MAX];
    snprintf(fileName, sizeof(fileName), nameFmt, fileSize, bufferSize);

    beginTest(fileName);

	openFile(pipe, fileName);

    generateFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);

    appendFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize+bufferSize, 16*1024);

    /* Clean things up */
    deleteFile(pipe, fileName);
}


/* run a matrix of tests for various file sizes and buffer sizes */
void streamTest(IoStack *pipe, char *nameFmt)
{
    size_t fileSize[] = {1024, 0, 64, 1027, 1, 1024*1024, 64*1024*1024 + 127};
    size_t bufSize[] = {1024, 32*1024, 64, 1};
#define countof(array) (sizeof(array)/sizeof(array[0]))

    for (int fileIdx = 0; fileIdx<countof(fileSize); fileIdx++)
        for (int bufIdx = 0; bufIdx<countof(bufSize); bufIdx++)
            singleStreamTest(pipe, nameFmt, fileSize[fileIdx], bufSize[bufIdx]);
}



/* Run a test on a single configuration determined by file size and buffer size */
void singleReadSeekTest(IoStack *pipe, char *nameFmt, size_t fileSize, size_t bufferSize)
{
    char fileName[PATH_MAX];
    snprintf(fileName, sizeof(fileName), nameFmt, fileSize, bufferSize);

    beginTest(fileName);

	openFile(pipe, fileName);

    generateFile(pipe, fileName, fileSize, bufferSize);
    verifyFile(pipe, fileName, fileSize, bufferSize);

    verifyRandomFile(pipe, fileName, fileSize, bufferSize);

    appendFile(pipe, fileName, fileSize, bufferSize);
    verifyRandomFile(pipe, fileName, fileSize+bufferSize, bufferSize);

    /* Clean things up */
    deleteFile(pipe, fileName);
}


/* run a matrix of tests for various file sizes and buffer sizes */
void readSeekTest(IoStack *pipe, char *nameFmt)
{
    size_t fileSize[] = {1024, 0, 64, 1027, 1, 1024*1024, 64*1024*1024 + 127};
    size_t bufSize[] = {1024, 32*1024, 64, 1};
#define countof(array) (sizeof(array)/sizeof(array[0]))

    for (int fileIdx = 0; fileIdx<countof(fileSize); fileIdx++)
        for (int bufIdx = 0; bufIdx<countof(bufSize); bufIdx++)
            singleStreamTest(pipe, nameFmt, fileSize[fileIdx], bufSize[bufIdx]);
}
