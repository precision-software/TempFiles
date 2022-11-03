/**
 *
 */
#include "common/passThrough.h"
#include "fileSplit.h"

static void closeCurrentSegment(FileSplit Stage *this, Error *error);
static void openCurrentSegment(FileSplitFilter *this, Error *error);

struct FileSplitFilter
{
    Stage filter;
    size_t segmentSize;
    PathGetter getPath;
    void *pathData;
    size_t position;
    char name[PATH_MAX];  /* Name of the file set. We could allocate it dynamically. */
    int mode;
    int perm;
};

static const Error errorPathTooLong = (Error){.code=errorCodeFilter, .msg="File path is too long"};

Error fileSplitOpen(FileSplitFilter *this, char *name, int mode, int perm)
{
    this->mode = mode;
    this->perm = perm;
    if (strlen(name) >= sizeof(this->name))
        return errorPathTooLong;
    strcpy(this->name, name);

    /* Position at the beginning of the first segment, creating it if writing. */
    this->position = 0;
    Error error = errorOK;
    openCurrentSegment(this, &error);

    return error;
}

size_t fileSplitRead(FileSplitFilter *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error)) return 0;

    /* If crossing segment boundary, then truncate to the end of segment. */
    size_t start = this->position % this->segmentSize;
    size_t truncSize = sizeMin( this->segmentSize - start, size);

    /* Read the possibly truncated buffer. */
    size_t actual = passThroughRead(this, buf, truncSize, error);
    this->position += actual;

    /* If we just finished reading an entire segment, advance to the next segment. */
    /*    Note we always have a partial segment at the end of the sequence, so we aren't at the end yet. */
    if (start + actual == this->segmentSize)
        openCurrentSegment(this, error);

    return actual;
}

size_t fileSplitWrite(FileSplitFilter *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error)) return 0;

    /* If crossing segment boundary, then truncate to the end of segment. */
    size_t start = this->position % this->segmentSize;
    size_t truncSize = sizeMin( this->segmentSize - start, size);

    /* Write the possibly truncated buffer. */
    size_t actual = passThroughWrite(this, buf, truncSize, error);
    this->position += actual;

    /* If we have filled the current segment, then open up the next segment. */
    /*  Note we must always end the sequence with a partial segment, even if it is zero length. */
    if (start + actual == this->segmentSize)
        openCurrentSegment(this, error);

    return actual;
}

void fileSplitClose(FileSplitFilter *this, Error *error)
{
    closeCurrentSegment(this, error);
}


static void closeCurrentSegment(FileSplitFilter *this, Error *error)
{
    if (this->position > 0)
        passThroughClose(this, error);

}

static void openCurrentSegment(FileSplitFilter *this, Error *error)
{
    /* Start by closing the current segment, of it was opened. */
    closeCurrentSegment(this, error);
    if (isError(*error))
        return;

    /* Generate the path to the new file segment. */
    size_t segmentIdx = this->position / this->segmentSize;
    char path[PATH_MAX];
    this->getPath(this->pathData, this->name, segmentIdx, path);

    /* Open the new file segment. */
    /* Note if we exit now, the last file will be zero length, which will be read back as an EOF. */
    *error = passThroughOpen(this, path, this->mode, this->perm);
}

size_t fileSplitSize(FileSplitFilter *this, size_t writeSize)
{
    this->filter.writeSize = writeSize;
    this->filter.readSize = passThroughSize(this, writeSize);
    return this->filter.readSize;
}

static PipelineInterface fileSplitInterface = {
    .fnClose = (FilterClose)fileSplitClose,
    .fnOpen = (FilterOpen)fileSplitOpen,
    .fnRead = (FilterRead)fileSplitRead,
    .fnWrite = (FilterWrite)fileSplitWrite,
    .fnSize = (FilterSize)fileSplitSize
};

Stage *fileSplitFilterNew(size_t segmentSize, PathGetter getPath, void *pathData, Stage *next)
{
    FileSplitFilter *this = malloc(sizeof(FileSplitFilter));
    *this = (FileSplitFilter) {
        .getPath = getPath,
        .pathData = pathData,
        .segmentSize = segmentSize
    };
    return filterInit(this, &fileSplitInterface, next);
}

void formatPath(void *fmt, char *name, size_t segmentIdx, char path[PATH_MAX])
{
    snprintf(path, PATH_MAX, fmt, name, segmentIdx);
}
