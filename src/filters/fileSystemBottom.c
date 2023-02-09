/**
 * FileSystemBottom is the consumer of file system events, doing the actual
 * work of opening, closing, reading and writing files.
 * This particular sink works with a Posix file system, and it is
 * a straightforward wrapper around Posix system calls.
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include "../framework/debug.h"
#include "../iostack_internal.h"

/* A conventional POSIX file system for reading/writing a file. */
typedef struct FileSystemBottom FileSystemBottom;

struct FileSystemBottom {
    IoStack iostack;  			/* first in every Filter. */
    int fd;          			/* The file descriptor for the currrently open file. */
    bool writable;   			/* Can we write to the file? */
    bool readable;   			/* Can we read from the file? */
	bool open;
};


/**
 * Open a Posix file, returning true if sucessful.
 */
bool fileSystemOpen(FileSystemBottom *this, const char *path, int oflags, int perm)
{
    /* Check the oflags we use to open the file */
    this->writable = (oflags & O_ACCMODE) != O_RDONLY;
    this->readable = (oflags & O_ACCMODE) != O_WRONLY;
    fileClearError(this);

    /* Default file permission when creating a file. */
    if (perm == 0)
        perm = 0666;

	/* Don't allow O_APPEND, as it changes behavior of pread/pwrite */
	oflags &= ~O_APPEND;

    /* Open the file and check for errors. */
    this->fd = open(path, oflags, perm);
	debug("filesystemOpen: path=%s oflags=0x%x  fd=%d\n", path, oflags, this->fd);

	return setSystemError(this, this->fd, "fileSystemOpen: ") != -1;
}


/**
 * Write data to a file. For efficiency, we like larger buffers,
 * but in a pinch we can write individual bytes.
 */
ssize_t fileSystemWrite(FileSystemBottom *this, const Byte *buf, size_t bufSize, off_t offset, void *ctx)
{
    /* Write the data. */
	errno = 0; /* TODO: remove */
    ssize_t ret = pwrite(this->fd, buf, bufSize, offset);
	debug("fileSystemWrite: fd=%d bufSize=%zu offset=%lld  ret=%zd errno=%d\n", this->fd, bufSize, offset, ret, errno);

	/* If unable to write the entire buffer, assume ran out of space. */
	/* TODO: EINTR? */
	if (ret >= 0 && ret != bufSize)
	{
		ret = -1;
		errno = ENOSPC;
	}

	return setSystemError(this, ret, "fileSystemWrite");
}


/**
 * Read data from a file, checking for EOF. We like larger read buffers
 * for efficiency, but they are not required.
 */
ssize_t fileSystemRead(FileSystemBottom *this, Byte *buf, size_t size, off_t offset)
{
    // Do the actual read.
	errno = 0; /* TODO: remove */
    ssize_t ret = pread(this->fd, buf, size, offset);
	debug("fileSystemRead: fd=%d size=%zu offset=%lld  ret=%zd  errno=%d\n", this->fd, size, offset, ret, errno);

	/* Check for end of file. */
	this->iostack.eof = (ret ==  0);

	return setSystemError(this, ret, "fileSystemRead");
}


/**
 * Close a Posix file.
 */
bool fileSystemClose(FileSystemBottom *this)
{
	debug("fileSystemClose: fd=%d\n", this->fd);
	ssize_t ret = 0;

    /* Close the fd if it was opened earlier. */
	if (this->fd != -1)
        ret = close(this->fd);
	this->fd = -1;

	/* If we had an earlier error, don't let close override it */
	if (this->iostack.errNo != 0)
		errno = this->iostack.errNo;

	return (setSystemError(this, ret, "fileSystemClose: ") == 0);

}

#define fdatasync(fd) fsync(fd)  /* TODO: Configure which sync call to use */

/**
 * Push data which has been written out to persistent storage.
 */
bool fileSystemSync(FileSystemBottom *this, void *cts)
{
	int ret = 0;

    /* Go sync it if we are writing to it.  (TODO: can others open readonly + sync?) */
    if (this->writable)
        ret = fdatasync(this->fd);

	return setSystemError(this, ret, "fileSystemSync") == 0;
}

bool fileSystemTruncate(FileSystemBottom *this, off_t offset)
{
	int ret = ftruncate(this->fd, offset);

	return setSystemError(this, ret, "fileSystemTruncate");
}

off_t fileSystemSize(FileSystemBottom *this)
{
	off_t size = lseek(this->fd, 0, SEEK_END);

	if (size == (off_t)-1)
		setSystemError(this, -1, "fileSystemSize");

	debug("fileSystemSize: fd=%d  size=%lld\n", this->fd, size);
	return size;
}


IoStackInterface fileSystemInterface = (IoStackInterface)
{
    .fnOpen = (IoStackOpen)fileSystemOpen,
    .fnWrite = (IoStackWrite)fileSystemWrite,
    .fnRead = (IoStackRead)fileSystemRead,
    .fnClose = (IoStackClose)fileSystemClose,
    .fnSync = (IoStackSync)fileSystemSync,
    .fnTruncate = (IoStackTruncate)fileSystemTruncate,
	.fnSize = (IoStackSize)fileSystemSize,
};


/**
 * Create a new Posix file system Sink.
 */
IoStack *fileSystemBottomNew()
{
    FileSystemBottom *this = malloc(sizeof(FileSystemBottom));
    *this = (FileSystemBottom)
    {
        .fd = -1,
        .iostack = (IoStack){
            .iface=&fileSystemInterface,
            .next=NULL,
			.blockSize = 1,
			}
    };
    return (IoStack *)this;
}
