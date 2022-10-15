//
// Created by John Morris on 10/15/22.
//

#ifndef FILTER_SYSCALL_H
#define FILTER_SYSCALL_H

#include <stddef.h>
#include "common/error.h"

int sys_open(char *path, int oflag, int perm, Error *error);
size_t sys_read(int fd, Byte *buf, size_t size, Error *error);
size_t sys_write(int fd, Byte *buf, size_t size, Error *error);
void sys_close(int fd, Error *error);


#endif //FILTER_SYSCALL_H
