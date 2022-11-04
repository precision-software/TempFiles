/**
 * A collection of system call wrappers, packaged to use our error handling objects.
 */
#include <unistd.h>
#include <sys/fcntl.h>
#include "syscall.h"


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

    return fd;
}

/**
 * Close a file, reporting an error if none occurred so far.
 */

void sys_close(int fd, Error *error)
{
    if (fd != -1 && close(fd) == -1 && errorIsOK(*error))
        *error = systemError();
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
