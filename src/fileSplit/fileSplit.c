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
 *      splitter = fileSplitNew(64*1024*1024, formatPath, "/tmp/postgres/%s-%d.dat", next)
 *  The segments would be named  /tmp/postgres/NAME-0.dat, /tmp/postgres/NAME-1.dat and so on.
 *
 *  TODO: improve random access performance by keeping segment files open if we seek away from them.
 *  TODO: when given O_TRUNC, delete all segments except the first.
 */
//#define DEBUG
#include <stdlib.h>
#include <sys/fcntl.h>
#include "common/debug.h"
#include "common/passThrough.h"
#include "fileSplit/fileSplit.h"
#include "file/fileSource.h"

/* Structure defining the state for read/writing a group of split files */
struct FileSplit
{
    Filter filter;      /* Common to all "filters" */

    /* Configuration */
    size_t suggestedSize; /* Suggested number of bytes each segment will hold, except last. */
    PathGetter getPath;   /* Function to calculate the name of each file segment. */
    void *pathData;       /* Object pased to the getPath function */

    /* Current state  */
    size_t segmentSize;   /* Actual number of bytes each segment will hold, except last */
    size_t position;      /* Current position within the group of files. */
    char name[PATH_MAX];  /* Name of the overall file set, used to calculate segment names. */
    int oflags;           /* The mode we open each segment in. */
    int perm;             /* If creating files, use this permission. */
    FileSource *file;     /* points to the current open file segment, null otherwise */
};

static const Error errorPathTooLong = (Error){.code=errorCodeFilter, .msg="File path is too long"};

static void closeCurrentSegment(FileSplit *this, Error *error);
static void openCurrentSegment(FileSplit *this, Error *error);
pos_t fileSplitSeekEnd(FileSplit *this, Error *error);

/**
 * Open a set of split files. These are a group of files which, when appended
 * together, can be treated as though they are a single file.
 * @param name - The logical file name of the group of files.
 *               Each individual file will have it's own generated name.
 * @param mode - The "O_mode" to open the file
 * @param perm - If creating a file, the Posix style permissions of the new file.
 * @return     - Error code indicating "OK" or error.
 */
FileSplit *fileSplitOpen(FileSplit *self, char *name, int oflags, int perm, Error *error)
{
    /* Clone the following pipeline. A forced error will simply clone the pipeline and not open it. */
    Error ignoreError = errorEOF;
    Filter *clone = passThroughOpen(self, NULL, oflags, perm, &ignoreError);

    /* Clone ourselves */
    FileSplit *this = fileSplitNew(self->suggestedSize, self->getPath, self->pathData, clone);
    if (isError(*error))
        return this;

    /* We do not support O_APPEND directly. */
    if (oflags & O_APPEND)
        return (filterError(error, "fileSplit does not support O_APPEND - must use Buffered filter"), this);

    /* Save the open parameters since we will use them when opening each segment. */
    this->oflags = oflags;
    this->perm = perm;
    if (strlen(name) >= sizeof(this->name))
        return (filterError(error, "fileSplitOpen: path name too long"), this);
    strcpy(this->name, name);

    /* Position at the beginning of the first segment */
    this->position = 0;
    this->file = NULL;
    openCurrentSegment(this, error);

    /* TODO: If truncating, we need to remove any additional segments. */
    bool truncate = (oflags & O_TRUNC) == O_TRUNC;
    if (truncate)
        debug("FileSplit truncation not implented yet\n");

    /* We may need to create future segments, so add O_CREAT */
    if ((this->oflags & O_ACCMODE) != O_RDONLY)
        this->oflags |= O_CREAT;

    /* We truncated the first segment and removed successive segments. We do NOT want to truncate segments when opening them */
    this->oflags &= ~O_TRUNC;

    return this;
}

/**
 * Read from a set of split files as though they were a single file.
 */
size_t fileSplitRead(FileSplit *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error)) return 0;

    /* If crossing segment boundary, truncate to the end of segment. */
    size_t start = this->position % this->segmentSize;
    size_t truncSize = sizeMin( this->segmentSize - start, size);

    /* Read the possibly truncated buffer. */
    size_t actual = fileRead(this->file, buf, truncSize, error);
    this->position += actual;

    /* If we just finished reading an entire segment, advance to the next segment. */
    /*    Note we always have a partial segment at the end of the sequence, so we aren't at the end yet. */
    if (start + actual == this->segmentSize)
        openCurrentSegment(this, error);

    return actual;
}

pos_t fileSplitSeek(FileSplit *this, pos_t position, Error *error)
{
    /* Special case for seeking to end */
    if (position == FILE_END_POSITION)
        return fileSplitSeekEnd(this, error);

    pos_t oldPosition =  this->position;
    this->position = position;

    if (sizeRoundDown(position, this->segmentSize) != sizeRoundDown(oldPosition, this->segmentSize))
        openCurrentSegment(this, error);

    pos_t offset = position % this->segmentSize;
    fileSeek(this->file, offset, error);

    return position;
}

pos_t fileSplitSeekEnd(FileSplit *this, Error *error)
{
    /* Scan looking for last (partial) segment */
    /* TODO: make it a binary search instead of linear */
    size_t segmentCount;
    size_t segmentSize;

    /* Scan through the segments */
    for (segmentCount = 0; ; segmentCount++)
    {
        /* Get the size of the next segment */
        this->position = segmentCount * this->segmentSize;
        openCurrentSegment(this, error);
        segmentSize = fileSeek(this->file, FILE_END_POSITION, error);

        /* Done when we find a partial segment */
        if (segmentSize != this->segmentSize)
            break;
    }

    return segmentCount * this->segmentSize + segmentSize;

}

/**
 * Write to a group of split files, creating new segments if appropriate.
 */
size_t fileSplitWrite(FileSplit *this, Byte *buf, size_t size, Error *error)
{
    if (isError(*error)) return 0;

    /* If crossing segment boundary, then truncate write at the end of segment. */
    size_t offset = this->position % this->segmentSize;
    size_t truncSize = sizeMin( this->segmentSize - offset, size);

    /* Write the possibly truncated buffer. */
    size_t actual = fileWrite(this->file, buf, truncSize, error);
    this->position += actual;

    /* If we have filled the current segment, then open up the next segment. */
    /*  Note we must always end the sequence with a partial segment, even if it is zero length. */
    if (offset + actual == this->segmentSize)
        openCurrentSegment(this, error);

    return actual;
}

/**
 * Close the group of files.
 */
void fileSplitClose(FileSplit *this, Error *error)
{
    closeCurrentSegment(this, error);
    passThroughClose(this, error);
    free(this);
}

/**
 * Close the currently active segment.
 */
static void closeCurrentSegment(FileSplit *this, Error *error)
{
    if (this->file != NULL)
        fileClose(this->file, error);
    this->file = NULL;
}


/**
 * Open the segment corresponding to this->position.
 */
void openCurrentSegment(FileSplit *this, Error *error)
{
    /* Close the current segment if opened */
    closeCurrentSegment(this, error);

    /* Generate the path to the new file segment. */
    size_t segmentIdx = this->position / this->segmentSize;
    char path[PATH_MAX];
    this->getPath(this->pathData, this->name, segmentIdx, path);

    /* Open the new file segment. */
    this->file = fileSourceNew(passThroughOpen(this, path, this->oflags, this->perm, error));
}

/**
 * We don't transform data, so we just agree with the block sizes of our neighbors.
 */
size_t fileSplitBlockSize(FileSplit *this, size_t recordSize, Error *error)
{
    /* Pass through the size request */
    size_t nextSize = passThroughBlockSize(this, recordSize, error);

    /* Round up the segment size to contain an even number of records */
    this->segmentSize = sizeRoundUp(this->suggestedSize, recordSize);

    return nextSize;
}

static FilterInterface fileSplitInterface = {
    .fnClose = (FilterClose)fileSplitClose,
    .fnOpen = (FilterOpen)fileSplitOpen,
    .fnRead = (FilterRead)fileSplitRead,
    .fnWrite = (FilterWrite)fileSplitWrite,
    .fnSeek = (FilterSeek)fileSplitSeek,
    .fnBlockSize = (FilterBlockSize)fileSplitBlockSize
};

/**
 * Define a group of segmented files.
 * @param suggestedSize - the number of bytes in each segment (except the last)
 * @param getPath - A function, which given (pathData, name, segment index) generates a path.
 * @param pathData - opaque data used by getPath.
 * @param next - pointer to the next filter in the sequence.
 * @return - a constructed filter for segmenting files.
 */
FileSplit *fileSplitNew(size_t suggestedSize, PathGetter getPath, void *pathData, void *next)
{
    FileSplit *this = malloc(sizeof(FileSplit));
    *this = (FileSplit) {
        .getPath = getPath,
        .pathData = pathData,
        .suggestedSize = suggestedSize
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
