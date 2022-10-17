//
// Created by John Morris on 10/11/22.
//
#include <sys/fcntl.h>
#include <unistd.h>
#include "common/syscall.h"
#include "common/passThrough.h"
#include "fileSystemSink.h"
#include "common/request.h"

/* A conventional POSIX file system for reading/writing a file. */
struct FileSystemSink {
    Filter header;                                                  // first in every Filter.
    int fd;                                                         // The file descriptor for the currrently open file.
    bool writable;                                                  // Can we write to the file?
    bool readable;                                                  // Can we read from the file?
    bool eof;                                                       // Has the currently open file read past eof?
};

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
    // Truncate large unbuffered writes to a multiple of blockSize,
    // Smaller writes go through on the premise they are at the end of the file.
    size_t blockSize = this->header.blockSize;
    size_t size = sizeMax(bufSize, sizeRoundDown(bufSize, blockSize));

    // Write the data.
    return sys_write(this->fd, buf, size, error);
}

static Error errorReadTooSmall =
        (Error){.code=errorCodeFilter, .msg="unbuffered read request was smaller than block size", .causedBy=NULL};

size_t fileSystemRead(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    // Truncate large reads to a multiple of blockSize.
    size_t blockSize = this->header.blockSize;
    size_t size = sizeRoundDown(bufSize, blockSize);

    // Check for errors: existing, previous read had EOF, read must be at least one block in size.
    if (isError(*error))                 ;
    else if (this->eof)                  *error = errorEOF;
    else if (size < blockSize)           *error = errorReadTooSmall;

    return sys_read(this->fd, buf, size, error);
}

void fileSystemClose(FileSystemSink *this, Error *error)
{
    // Close the fd if it was opened earlier.
    sys_close(this->fd, error);
    this->fd = -1;
}

FilterInterface fileSystemInterface = (FilterInterface)
{
    .fnOpen = (FilterOpen)fileSystemOpen,
    .fnWrite = (FilterWrite)fileSystemWrite,
    .fnRead = (FilterRead)fileSystemRead,
    .fnClose = (FilterClose)fileSystemClose
};

Filter *fileSystemSinkNew()
{
    FileSystemSink *this = malloc(sizeof(FileSystemSink));
    *this = (FileSystemSink)
    {
        .fd = -1,
        .header = (Filter){
            .iface=&fileSystemInterface,
            .blockSize = 16*1024,
            .next=NULL}
    };
    setNext(this);
    return (Filter *)this;
}
