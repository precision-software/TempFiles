//
// Created by John Morris on 10/15/22.
//
#include <lz4frame.h>
#include <sys/fcntl.h>
#include "compress/lz4/lz4_internal.h"

Error lz4FilterOpen(Lz4Filter *this, char *path, int mode, int perm)
{
    // We support I/O in only one direction per open.
    this->readable = (mode & O_ACCMODE) != O_WRONLY;
    this->writeable = (mode & O_ACCMODE) != O_RDONLY;
    if (this->readable == this->writeable)
        return errorCantBothReadWrite;

    // Continue the open based on whether we are reading or writing.
    if (this->writeable)
        return lz4CompressOpen(this, path, mode, perm);
    else
        return lz4DecompressOpen(this, path, mode, perm);
}

void lz4FilterClose(Lz4Filter *this, Error *error)
{
    if (this->writeable)
        lz4CompressClose(this, error);
    else
        lz4DecompressClose(this, error);
}


Filter *lz4FilterNew(Filter *next, size_t blockSize)
{
    // Set up our encryption preferences. In particular, minimize buffering during compression.
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.autoFlush = 1;
    size_t bufferSize = LZ4F_compressBound(blockSize, &preferences);

    Lz4Filter *this = malloc(sizeof(Lz4Filter));
    *this = (Lz4Filter) {
        .header = (Filter) {
            .blockSize = 1,
            .iface = &lz4Interface,
            .next = next,
        },
        .preferences = preferences,
        .blockSize = blockSize,
        .buf = bufferNew(bufferSize)
    };
    setNext(this);
    return (Filter *)this;
}



void lz4FilterFree(Lz4Filter *this)
{

}


FilterInterface lz4Interface = {
        .fnOpen = (FilterOpen)lz4FilterOpen,
        .fnRead = (FilterRead)lz4DecompressRead,
        .fnWrite = (FilterWrite)lz4CompressWrite,
        .fnClose = (FilterClose)lz4FilterClose,
};
