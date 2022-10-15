//
// Created by John Morris on 10/15/22.
//


#include <unistd.h>
#include <sys/fcntl.h>
#include "syscall.h"


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
