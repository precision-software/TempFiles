/**
 * A collection of system call wrappers, packaged to use our error handling objects.
 */
//#define DEBUG
#include <unistd.h>
#include <sys/fcntl.h>
#include "common/syscall.h"
#include "common/debug.h"


/**
 * Read data from a file.
 * @param error - if set on entry, return immediately. On exit, contains OK, EOF or error.
 */
size_t sys_read(int fd, Byte *buf, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    ssize_t retVal = read(fd, buf, size);

    if (retVal == 0)
        *error = errorEOF;

    else if (retVal == -1)
    {
        *error = systemError();
        retVal = 0;
    }

    debug("sys_read: fd=%d size=%zu actual=%zd  msg=%s\n", fd, size, retVal, error->msg);
    return (size_t) retVal;
}


/**
 * Write bytes to a file, respecting error handling conventions.
 * @param error - if set on entry, return immediately. On exit, contains OK, EOF or error.
 */
size_t sys_write(int fd, Byte *buf, size_t size, Error *error)
{
    if (isError(*error))
        return 0;

    ssize_t retVal = write(fd, buf, size);
    if (retVal == -1)
    {
        *error = systemError();
        retVal = 0;
    }

    debug("sys_write: fd=%d size=%zu  msg=%s\n", fd, size, error->msg);

    return (size_t) retVal;

}


/**
 * Open a file, respecting error handling conventions.
 *  @param error - if set on entry, return immediately. On exit, contains OK, EOF or error.
 */
int sys_open(char *path, int oflag, int perm, Error *error)
{
    if (isError(*error)) return -1;

    int fd = open(path, oflag, perm);
    if (fd == -1)
        *error = systemError();

    debug("sys_open: fd=%d  path=%s  oflags=0x%x msg=%s\n", fd, path, oflag, error->msg);
    return fd;
}

/**
 * Close a file, reporting an error if none occurred so far.
 */

void sys_close(int fd, Error *error)
{
    if (fd != -1 && close(fd) == -1 && errorIsOK(*error))
        *error = systemError();
    debug("sys_close: fd=%d  msg=%s\n", fd, error->msg);
}


/**
 * Synchronize a file to persistent storage, reporting error if none occurred so far.
 */
#define fdatasync(fd) fsync(fd)
void sys_datasync(int fd, Error *error)
{
    if (fdatasync(fd) == -1 && errorIsOK(*error))
        *error = systemError();
}

/* We don't allow holes so use SEEK_DATA if it is available */
#ifndef SEEK_DATA
#define SEEK_DATA SEEK_SET
#endif

/**
 * Seek to an absolute file position.
 */
pos_t sys_lseek(int fd, pos_t position, Error *error)
{
    if (isError(*error))
        return (pos_t)-1;

    off_t ret;
    if (position == FILE_END_POSITION)
        ret = lseek(fd, 0, SEEK_END);

    else
    {
        ret = lseek(fd, (off_t) position, SEEK_SET);
        if (ret != -1 && ret != position)
            filterError(error, "Seeking beyond end of file - holes not allowed");
    }

    if (ret == -1)
        *error = systemError();

    debug("sys_lseek: fd=%d  position=%lld  ret=%lld\n", fd, (off_t)position, ret);
    return (pos_t)ret;
}


void sys_unlink(char *path, Error *error)
{
    int ret = unlink(path);
    if (ret == -1)
        setError(error, systemError());
    debug("sys_unlink: path=%s  msg=%s\n", path, error->msg);
}
