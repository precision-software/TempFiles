//
// Created by John Morris on 10/16/22.
//
#include "common/passThrough.h"
#include "fileSet/fileSet.h"

static void closeCurrentSegment(FileSetFilter *this, Error *error);
static void openCurrentSegment(FileSetFilter *this, Error *error);

struct FileSetFilter
{
    Filter header;
    size_t segmentSize;
    PathGetter getPath;
    void *pathData;
    size_t position;
    char name[PATH_MAX];  // Name of the file set. We could allocate it dynamically.
    int mode;
    int perm;
};

Error errorPathTooLong = (Error){.code=errorCodeFilter, .msg="File path is too long"};

Error fileSetOpen(FileSetFilter *this, char *name, int mode, int perm)
{
    this->mode = mode;
    this->perm = perm;
    if (strlen(name) >= sizeof(this->name))
        return errorPathTooLong;
    strcpy(this->name, name);

    // Position at the beginning of the first segment.
    this->position = 0;
    Error error = errorOK;
    openCurrentSegment(this, &error);

    return error;
}

size_t fileSetRead(FileSetFilter *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error)) return 0;

    // If starting a new segment, then open the new segment.
    size_t start = this->position % this->segmentSize;
    if (start == 0)
        openCurrentSegment(this, error);

    // If crossing segment boundary, then truncate to the end of segment.
    size_t truncSize = sizeMin( this->segmentSize - start, size);

    // Read the possibly truncated buffer.
    size_t actual = passThroughRead(this, buf, truncSize, error);
    this->position += actual;

    return actual;
}

size_t fileSetWrite(FileSetFilter *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error)) return 0;

    // If starting a new segment, then open the new segment.
    size_t start = this->position % this->segmentSize;
    if (start == 0)
        openCurrentSegment(this, error);

    // If crossing segment boundary, then truncate to the end of segment.
    size_t truncSize = sizeMin( this->segmentSize - start, size);

    // Write the possibly truncated buffer.
    size_t actual = passThroughWrite(this, buf, truncSize, error);
    this->position += actual;

    return actual;
}

void fileSetClose(FileSetFilter *this, Error *error)
{
    closeCurrentSegment(this, error);
}


static void closeCurrentSegment(FileSetFilter *this, Error *error)
{
    if (this->position > 0)
        passThroughClose(this, error);

}

static void openCurrentSegment(FileSetFilter *this, Error *error)
{
    // Start by closing the current segment, of it was opened.
    closeCurrentSegment(this, error);
    if (isError(*error))
        return;

    // Generate the path to the new file segment.
    size_t segmentIdx = this->position / this->segmentSize;
    char path[PATH_MAX];
    this->getPath(this->pathData, this->name, segmentIdx, path);

    // Open the new file segment.
    // Note if we exit now, the last file will be zero length, which will be read back as an EOF.
    *error = passThroughOpen(this, path, this->mode, this->perm);
}

static FilterInterface fileSetInterface = {
    .fnClose = (FilterClose)fileSetClose,
    .fnOpen = (FilterOpen)fileSetOpen,
    .fnRead = (FilterRead)fileSetRead,
    .fnWrite = (FilterWrite)fileSetWrite
};

Filter *fileSetFilterNew(Filter *next, size_t segmentSize, PathGetter getPath, void *pathData)
{
    FileSetFilter *this = malloc(sizeof(FileSetFilter));
    *this = (FileSetFilter) {
        .header = (Filter) {
            .next = next,
            .blockSize = next->blockSize,
            .iface = &fileSetInterface
        },
        .getPath = getPath,
        .pathData = pathData,
        .segmentSize = segmentSize
    };
    return (Filter *)this;
}

void formatPath(void *fmt, char *name, size_t segmentIdx, char path[PATH_MAX])
{
    snprintf(path, PATH_MAX, fmt, name, segmentIdx);
}
