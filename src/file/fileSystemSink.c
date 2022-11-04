/**
 * FileSystemSink is the consumer of file system events, doing the actual
 * work of opening, closing, reading and writing files.
 * This particular sink works with a Posix file system, and it is
 * a straightforward wrapper around Posix system calls.
 */
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
Error fileSystemOpen(FileSystemSink *this, char *path, int mode, int perm)
{
    /* Check the mode we are opening the file in. */
    this->writable = (mode & O_ACCMODE) != O_RDONLY;
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->eof = false;

    /* Default file permission when creating a file. */
    if (perm == 0)
        perm = 0666;

    /* Open the file and check for errors. */
    Error error = errorOK;
    this->fd = sys_open(path, mode, perm, &error);

    return error;
}


/**
 * Write data to a file. For efficiency, we like larger buffers,
 * but in a pinch we can write individual bytes.
 */
size_t fileSystemWrite(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    /* Check for errors. */
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
    this->fd = -1;
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
 * We declare a large block size to encourage those
 * above us to buffer, but we probably should declare
 * a single byte and let them make their own decisions.
 */
size_t fileSystemSize(FileSystemSink *this, size_t size)
{
    this->filter.writeSize = size;
    this->filter.readSize = 16*1024;
    return this->filter.readSize;
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


FilterInterface fileSystemInterface = (FilterInterface)
{
    .fnOpen = (FilterOpen)fileSystemOpen,
    .fnWrite = (FilterWrite)fileSystemWrite,
    .fnRead = (FilterRead)fileSystemRead,
    .fnClose = (FilterClose)fileSystemClose,
    .fnSync = (FilterSync)fileSystemSync,
    .fnSize = (FilterSize)fileSystemSize,
    .fnAbort = (FilterAbort)fileSystemAbort,
};


/**
 * Create a new Posix file system Sink.
 */
Filter *fileSystemSinkNew()
{
    FileSystemSink *this = malloc(sizeof(FileSystemSink));
    *this = (FileSystemSink)
    {
        .fd = -1,
        .filter = (Filter){
            .iface=&fileSystemInterface,
            .next=NULL}
    };
    return (Filter *)this;
}
