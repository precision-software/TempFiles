/**
 * FileSystemSink is the consumer of file system events, doing the actual
 * work of opening, closing, reading and writing files.
 * This particular sink works with a Posix file system, and it is
 * a straightforward wrapper around Posix system calls.
 */
#include <stdlib.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include "common/syscall.h"
#include "common/passThrough.h"
#include "fileSystemSink.h"

/* A conventional POSIX file system for reading/writing a file. */
struct FileSystemSink {
    Filter filter;   /* first in every Filter. */
    int fd;          /* The file descriptor for the currrently open file. */
    bool writable;   /* Can we write to the file? */
    bool readable;   /* Can we read from the file? */
    bool eof;        /* Has the currently open file read past eof? */
};

static Error errorCantWrite = (Error){.code=errorCodeFilter, .msg="Writing to file opened as readonly"};
static Error errorCantRead = (Error){.code=errorCodeFilter, .msg="Reading from file opened as writeonly"};
static Error errorReadTooSmall = (Error){.code=errorCodeFilter, .msg="unbuffered read was smaller than block size"};


/**
 * Open a Posix file.
 */
FileSystemSink *fileSystemOpen(FileSystemSink *sink, char *path, int oflags, int perm, Error *error)
{
    /* Clone ourself. */
    FileSystemSink *this = fileSystemSinkNew();

    /* Check the oflags we are opening the file in. TODO: move checks to fileSource. */
    this->writable = (oflags & O_ACCMODE) != O_RDONLY;
    this->readable = (oflags & O_ACCMODE) != O_WRONLY;
    this->eof = false;

    /* Default file permission when creating a file. */
    if (perm == 0)
        perm = 0666;

    /* Open the file and check for errors. */
    this->fd = sys_open(path, oflags, perm, error);

    return this;
}


/**
 * Write data to a file. For efficiency, we like larger buffers,
 * but in a pinch we can write individual bytes.
 */
size_t fileSystemWrite(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    /* Check for errors. TODO: move to fileSource. */
    if (errorIsOK(*error) && !this->writable)
        *error = errorCantWrite;

    /* Write the data. */
    return sys_write(this->fd, buf, bufSize, error);
}


/**
 * Read data from a file, checking for EOF. We like larger read buffers
 * for efficiency, but they are not required.
 */
size_t fileSystemRead(FileSystemSink *this, Byte *buf, size_t size, Error *error)
{

    /* Check for errors. */
    if (isError(*error))                 ;
    else if (!this->readable)            *error = errorCantRead;
    else if (this->eof)                  *error = errorEOF;

    // Do the actual read.
    return sys_read(this->fd, buf, size, error);
}


/**
 * Close a Posix file.
 */
void fileSystemClose(FileSystemSink *this, Error *error)
{
    /* Close the fd if it was opened earlier. */
    sys_close(this->fd, error);
    free(this);
}


/**
 * Push data which has been written out to persistent storage.
 */
void fileSystemSync(FileSystemSink *this, Error *error)
{
    /* Error if file was readonly. */
    if (errorIsOK(*error) && !this->writable)
        *error = errorCantWrite;

    /* Go sync it. */
    if (this->writable)
        sys_datasync(this->fd, error);
}


/**
 * Negotiate the block size for reading and writing.
 * Since we are not supporting O_DIRECT yet, we simply
 * return 1 indicating we can deal with any size.
 */
size_t fileSystemBlockSize(FileSystemSink *this, size_t prevSize, Error *error)
{
    return 1;
}


/**
 * Abort file access, removing any files we may have created.
 * This allows us to remove temporary files when a transaction aborts.
 * Not currently implemented.
 */
void fileSystemAbort(FileSystemSink *this, Error *errorO)
{
    abort(); /* TODO: not implemented. */
}

pos_t fileSystemSeek(FileSystemSink *this, pos_t position, Error *error)
{
    return sys_lseek(this->fd, position, error);
}


void fileSystemDelete(FileSystemSink *this, char *path, Error *error)
{
    /* Unlink the file, even if we've already had an error */
    Error tempError = errorOK;
    sys_unlink(path, &tempError);
    setError(error, tempError);

}

FilterInterface fileSystemInterface = (FilterInterface)
{
    .fnOpen = (FilterOpen)fileSystemOpen,
    .fnWrite = (FilterWrite)fileSystemWrite,
    .fnRead = (FilterRead)fileSystemRead,
    .fnClose = (FilterClose)fileSystemClose,
    .fnSync = (FilterSync)fileSystemSync,
    .fnBlockSize = (FilterBlockSize)fileSystemBlockSize,
    .fnAbort = (FilterAbort)fileSystemAbort,
    .fnSeek = (FilterSeek)fileSystemSeek,
    .fnDelete = (FilterDelete)fileSystemDelete
};


/**
 * Create a new Posix file system Sink.
 */
FileSystemSink *fileSystemSinkNew()
{
    FileSystemSink *this = malloc(sizeof(FileSystemSink));
    *this = (FileSystemSink)
    {
        .fd = -1,
        .filter = (Filter){
            .iface=&fileSystemInterface,
            .next=NULL}
    };
    return this;
}
