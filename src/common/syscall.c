/**
 * Wrappers around common system calls, primarily to do more convenient error handling.
 */
#include <unistd.h>
#include <sys/fcntl.h>
#include "common/syscall.h"
#include "common/error.h"

/**
 * Do a read() system call, with error handling.
 *    If an error occurred previously, do nothing.
 *    If the read() call errored just now, update the "error" variable to record it.
 *  return the number of bytes transferred, which is 0 in the case of an error or EOF.
 */
size_t sys_read(int fd, Byte *buf, size_t size, Error *error)
{
    /* See if an error occurred earlier. If so, done. */
    if (isError(*error))
        return 0;

    /* Issue the kernel read call */
    ssize_t retVal = read(fd, buf, size);

    /* CASE: end of file */
    if (retVal == 0)
        *error = errorEOF;

    /* CASE: system error */
    else if (retVal == -1)
    {
        *error = systemError();
        retVal = 0;
    }

    /* OTHERWISE, successful */
    return (size_t) retVal;
}

/**
 * Do a write() system call, with error handling,
 */
size_t sys_write(int fd, Byte *buf, size_t size, Error *error)
{
    /* See if an error occurred earlier. If so, done. */
    if (isError(*error))
        return 0;

    // Issue the write() system call and check for errors */
    ssize_t retVal = write(fd, buf, size);
    if (retVal == -1)
    {
        *error = systemError();
        retVal = 0;
    }

    // Return the number of bytes read. (zero if error)
    return (size_t) retVal;
}


int sys_open(char *path, int oflag, int perm, Error *error)
{
    if (isError(*error)) return -1;

    int fd = open(path, oflag, perm);
    if (fd == -1)
        *error = systemError();

    return fd;
}


void sys_close(int fd, Error *error)
{
    if (fd != -1 && close(fd) == -1 && errorIsOK(*error))
        *error = systemError();
}

#define fdatasync(fd) fsync(fd)
void sys_datasync(int fd, Error *error)
{
    if (fdatasync(fd) == -1 && errorIsOK(*error))
        *error = systemError();
}
