/**
 * Wrappers around system calls.  TODO: move comments here from system_call.c.
 */
#ifndef FILTER_SYSCALL_H
#define FILTER_SYSCALL_H

#include <stddef.h>
#include "iostack_error.h"
#include "common/filter.h" /* for off_t and FILE_END_POSITION */

int sys_open(const char *path, int oflag, int perm, Error *error);
size_t sys_read(int fd, Byte *buf, size_t size, Error *error);
size_t sys_write(int fd, const Byte *buf, size_t size, Error *error);
void sys_close(int fd, Error *error);
void sys_datasync(int fd, Error *error);
off_t sys_lseek(int fd, off_t position, Error *error);
void sys_unlink(char *path, Error *error);


#endif /*FILTER_SYSCALL_H */
