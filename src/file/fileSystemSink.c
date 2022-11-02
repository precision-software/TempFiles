//
// Created by John Morris on 10/11/22.
//
#include <sys/fcntl.h>
#include <unistd.h>
#include "common/syscall.h"
#include "common/passThrough.h"
#include "fileSystemSink.h"

/* A conventional POSIX file system for reading/writing a file. */
struct FileSystemSink {
    Filter filter;                                                  // first in every Filter.
    int fd;                                                         // The file descriptor for the currrently open file.
    bool writable;                                                  // Can we write to the file?
    bool readable;                                                  // Can we read from the file?
    bool eof;                                                       // Has the currently open file read past eof?
    size_t blockSize;
};

static Error errorCantWrite = (Error){.code=errorCodeFilter, .msg="Writing to file opened as readonly"};
static Error errorCantRead = (Error){.code=errorCodeFilter, .msg="Reading from file opened as writeonly"};
static Error errorReadTooSmall = (Error){.code=errorCodeFilter, .msg="unbuffered read was smaller than block size"};

Error fileSystemOpen(FileSystemSink *this, char *path, int mode, int perm)
{
    // Check the mode we are opening the file in.
    this->writable = (mode & O_ACCMODE) != O_RDONLY;
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->eof = false;

    // Default file permission when creating a file.
    if (perm == 0)
        perm = 0666;

    // Open the file and check for errors.
    Error error = errorOK;
    this->fd = sys_open(path, mode, perm, &error);

    return error;
}

size_t fileSystemWrite(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    // Check for errors.
    if (errorIsOK(*error) && !this->writable)
        *error = errorCantWrite;

    // Truncate large unbuffered writes to a multiple of blockSize,
    // Smaller writes go through on the premise they are at the end of the file.
    size_t size = sizeMax(bufSize, sizeRoundDown(bufSize, this->blockSize));

    // Write the data.
    return sys_write(this->fd, buf, size, error);
}

size_t fileSystemRead(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    // Truncate large reads to a multiple of blockSize.
    size_t size = bufSize; //sizeRoundDown(bufSize, this->blockSize);

    // Check for errors.
    if (isError(*error))                 ;
    else if (!this->readable)            *error = errorCantRead;
    else if (this->eof)                  *error = errorEOF;
    //else if (size < this->blockSize)     *error = errorReadTooSmall;

    return sys_read(this->fd, buf, size, error);
}

void fileSystemClose(FileSystemSink *this, Error *error)
{
    // Close the fd if it was opened earlier.
    sys_close(this->fd, error);
    this->fd = -1;
}

void fileSystemSync(FileSystemSink *this, Error *error)
{
    // Error if file was readonly.
    if (errorIsOK(*error) && !this->writable)
        *error = errorCantWrite;

    // Go sync it.
    if (this->writable)
        sys_datasync(this->fd, error);
}

size_t fileSystemSize(FileSystemSink *this, size_t size)
{
    this->filter.writeSize = size;
    this->filter.readSize = 16*1024;
    return this->filter.readSize;
}

void fileSystemAbort(FileSystemSink *this, Error *errorO)
{
    abort(); // TODO: not implemented.
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
