/**
 * Implement file splitting, where a group of related file segments
 * are treated as though they were a single file.
 *
 * File splitting can be used for several purposes.
 *  - Keep the size of any individual file small so it can be more easily managed.
 *  - Spread the segments round-robin across different directories to balance disk access.
 *
 * A group of files always terminates with a partially filled segment.
 * In the case where all segments are full, there will be a final, empty segment
 *
 *  The following example uses the included "formatPath" function to generate
 *  names of 64MB temporary data segments.
 *      splitter = fileSplitFilterNew(64*1024*1024, formatPath, "/tmp/postgres/%s-%d.dat", next)
 *  The segments would be named  /tmp/postgres/NAME-0.dat, /tmp/postgres/NAME-1.dat and so on.
 *
 *  Note Seek() is not yet supported.
 */
#include "common/passThrough.h"
#include "fileSplit.h"

/* Structure defining the state for read/writing a group of split files */
typedef struct FileSplitFilter
{
    Filter filter;      /* Common to all "filters" */
    size_t segmentSize; /* Number of bytes each segment will hold, except last. */
    PathGetter getPath; /* Function to calculate the name of each file segment. */
    void *pathData;     /* Object pased to the getPath function */
    size_t position;    /* Current position within the group of files. */
    char name[PATH_MAX];  /* Name of the overall file set, used to calculate segment names. */
    int mode;           /* The mode we open each segment in. */
    int perm;           /* If creating files, use this permission. */
} FileSplitFilter;

static const Error errorPathTooLong = (Error){.code=errorCodeFilter, .msg="File path is too long"};

static void closeCurrentSegment(FileSplitFilter *this, Error *error);
static void openCurrentSegment(FileSplitFilter *this, Error *error);

/**
 * Open a set of split files. These are a group of files which, when appended
 * together, can be treated as though they are a single file.
 * @param name - The logical file name of the group of files.
 *               Each individual file will have it's own generated name.
 * @param mode - The "O_mode" to open the file
 * @param perm - If creating a file, the Posix style permissions of the new file.
 * @return     - Error code indicating "OK" or error.
 */
Error fileSplitOpen(FileSplitFilter *this, char *name, int mode, int perm)
{
    /* Save the open parameters since we will use them when opening each segment. */
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

/**
 * Read from a set of split files as though they were a single file.
 */
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

/**
 * Write to a group of split files, creating new segments if appropriate.
 */
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

/**
 * Close the group of files.
 */
void fileSplitClose(FileSplitFilter *this, Error *error)
{
    closeCurrentSegment(this, error);
}

/**
 * Close the currently active segment.
 */
static void closeCurrentSegment(FileSplitFilter *this, Error *error)
{
    passThroughClose(this, error);
}


/**
 * Open the segment corresponding to this->position.
 */
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

/**
 * We don't transform data, so we just fit in with the block sizes of our neighbors.
 */
size_t fileSplitSize(FileSplitFilter *this, size_t writeSize)
{
    this->filter.writeSize = writeSize;
    this->filter.readSize = passThroughSize(this, writeSize);
    return this->filter.readSize;
}

static FilterInterface fileSplitInterface = {
    .fnClose = (FilterClose)fileSplitClose,
    .fnOpen = (FilterOpen)fileSplitOpen,
    .fnRead = (FilterRead)fileSplitRead,
    .fnWrite = (FilterWrite)fileSplitWrite,
    .fnSize = (FilterSize)fileSplitSize
};

/**
 * Define a group of segmented files.
 * @param segmentSize - the number of bytes in each segment (except the last)
 * @param getPath - A function, which given (pathData, name, segment index) generates a path.
 * @param pathData - opaque data used by getPath.
 * @param next - pointer to the next filter in the sequence.
 * @return - a constructed filter for segmenting files.
 */
Filter *fileSplitFilterNew(size_t segmentSize, PathGetter getPath, void *pathData, Filter *next)
{
    FileSplitFilter *this = malloc(sizeof(FileSplitFilter));
    *this = (FileSplitFilter) {
        .getPath = getPath,
        .pathData = pathData,
        .segmentSize = segmentSize
    };
    return filterInit(this, &fileSplitInterface, next);
}

/**
 * A typical segment name generator which uses a format statement to combine
 * the fileset name with segment index.
 */
void formatPath(void *fmt, char *name, size_t segmentIdx, char path[PATH_MAX])
{
    snprintf(path, PATH_MAX, fmt, name, segmentIdx);
}
