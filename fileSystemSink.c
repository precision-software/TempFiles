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
typedef struct FileSystemSink {
    Filter header;                                                  // first in every Filter.
    size_t blockSize;                                               // The unbuffered preferred size for I/O.
    int fd;                                                         // The file descriptor for the currrently open file.
    bool writable;                                                  // Can we write to the file?
    bool readable;                                                  // Can we read from the file?
    bool eof;                                                       // Has the currently open file read past eof?
} FileSystemSink;


void fileSystemOpen(FileSystemSink *this, OpenRequest *req)
{
    // Check the mode we are opening the file in.
    this->writable = (req->mode & O_ACCMODE) != O_RDONLY;
    this->readable = (req->mode & O_ACCMODE) != O_WRONLY;
    this->eof = false;

    // Default file permission when creating a file.
    int perm = (req->perm == 0)? 0666: req->perm;

    // Assume things are OK until proven otherwise.
    req->blockSize = this->blockSize;
    req->error = errorOK;

    // Open the file and check for errors.
    this->fd = open(req->path, req->mode, perm);
    if (this->fd == -1)
        req->error = systemError();
}

void fileSystemWrite(FileSystemSink *this, WriteRequest *req)
{
    // Truncate large unbuffered writes to a multiple of blockSize,
    // Smaller writes go through on the premise they are at the end of the file.
    size_t residual = req->bufSize % this->blockSize;
    size_t size = (req->bufSize < this->blockSize)? req->bufSize: req->bufSize - residual;

    // Assume things are good until proven otherwise.
    req->error = errorOK;

    // Write the data and check for errors.
    req->actualSize = write(this->fd, req->buf, size);
    if (req->actualSize == -1)
        req->error = systemError();
}

static Error errorReadTooSmall =
        (Error){.code=errorCodeFilter, .msg="unbuffered read request was smaller than block size", .causedBy=NULL};

void fileSystemRead(FileSystemSink *this, ReadRequest *req)
{
    // Truncate large reads to a multiple of blockSize.
    size_t residual = req->bufSize % this->blockSize;
    size_t size = req->bufSize - residual;

    // Assume everythihg is fine unless shown otherwise.
    req->error = errorOK;

    // CASE: we hit eof earlier, then just return eof.
    if (this->eof)
        req->error = errorEOF;

    // CASE: error - Unbuffered reads must be at least one block in size.
    else if (size < this->blockSize)
        req->error = errorReadTooSmall;

    // OTHERWISE: Normal read.
    else
    {
        // Read from the file and check for system errors.
        req->actualSize = read(this->fd, req->buf, size);
        if (req->actualSize == -1)
            req->error = systemError();

        // Also check for EOF;
        this->eof = req->actualSize == 0;
        if (this->eof)
            req->error = errorEOF;
    }
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

// The low level file system doesn't buffer, but sends data straight to the operating system.
void fileSystemPeek(FileSystemSink *this, PeekRequest *req)
{
    req->sinkBuf = NULL;
    req->error = errorOK;
}


FilterInterface fileSystemInterface = (FilterInterface)
{
    .fnOpen = (FilterService)fileSystemOpen,
    .fnWrite = (FilterService)fileSystemWrite,
    .fnRead = (FilterService)fileSystemRead,
    .fnClose = (FilterService)fileSystemClose,
    .fnPeek = (FilterService)fileSystemPeek,

    // Not implemented.
    .fnAbort = (FilterService)passThroughAbort,
    .fnSeek = (FilterService)passThroughSeek,
    .fnSync = (FilterService)passThroughSync
};

Filter *fileSystemSinkNew()
{
    FileSystemSink *this = malloc(sizeof(FileSystemSink));
    *this = (FileSystemSink)
    {
        .fd = -1,
        .blockSize = 16*1024,
        .header = (Filter){.iface=&fileSystemInterface, .next=NULL}
    };
    return (Filter *)this;
}
