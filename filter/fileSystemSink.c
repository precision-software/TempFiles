//
// Created by John Morris on 10/11/22.
//
#include <sys/fcntl.h>
#include <unistd.h>
#include "error.h"
#include "passThrough.h"
#include "fileSystemSink.h"
#include "request.h"


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
    this->fd = open(path, mode, perm);
    if (this->fd == -1)
        return systemError();

    return errorOK;
}

size_t fileSystemWrite(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    // Truncate large unbuffered writes to a multiple of blockSize,
    // Smaller writes go through on the premise they are at the end of the file.
    size_t blockSize = this->header.blockSize;
    size_t size = sizeMin(blockSize, sizeRoundDown(bufSize, blockSize));

    // Write the data and check for errors.
    size_t actualSize = write(this->fd, buf, size);
    if (actualSize == -1)
        *error = systemError();

    return actualSize;
}

static Error errorReadTooSmall =
        (Error){.code=errorCodeFilter, .msg="unbuffered read request was smaller than block size", .causedBy=NULL};

size_t fileSystemRead(FileSystemSink *this, Byte *buf, size_t bufSize, Error *error)
{
    // Truncate large reads to a multiple of blockSize.
    size_t blockSize = this->header.blockSize;
    size_t size = sizeRoundDown(bufSize, blockSize);

    size_t actualSize = 0;

    // CASE: we hit an error earlier, then just return the error
    if (!errorIsOK(*error))
        ;

    // CASE: we had eof on last read. Return eof now.
    else if (this->eof)
        *error = errorEOF;

    // CASE: error - Unbuffered reads must be at least one block in size.
    else if (size < blockSize)
        *error = errorReadTooSmall;

    // OTHERWISE: Normal read.
    else
    {
        // Read from the file and check for system errors.
        actualSize = read(this->fd, buf, size);
        if (actualSize == -1)
            *error = systemError();

        // Also check for EOF for future reads.
        this->eof = actualSize == 0;
        if (this->eof)
            *error = errorEOF;
    }

    if (errorIsOK(*error))
        return actualSize;
    else
        return 0;
}


void fileSystemClose(FileSystemSink *this, CloseRequest *req)
{
    req->error = errorOK;

    // Close the fd if it was opened earlier.
    if (this->fd != -1 && close(this->fd) == -1)
        req->error = systemError();

    // Don't try to close it a second time.
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
    return (Filter *)this;
}
