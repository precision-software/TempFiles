//
// Created by John Morris on 10/15/22.
//
#include <lz4frame.h>
#include <sys/fcntl.h>
#include "lz4_internal.h"

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


Filter *lz4FilterNew(size_t blockSize, Filter *next)
{
    // Set up our encryption preferences. In particular, minimize buffering during compression.
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    //preferences.autoFlush = 1;


    Lz4Filter *this = malloc(sizeof(Lz4Filter));

    *this = (Lz4Filter) {
        .preferences = preferences,
        .blockSize = blockSize, // TODO:
        .buf = bufferNew(bufferSize)  // TODO:
    };

    return filterInit(this, &lz4Interface, next);
}



void lz4FilterFree(Lz4Filter *this)
{

}

size_t lz4FilterSize(Lz4Filter *this, size_t writeSize)
{
    this->filter.writeSize = sizeMax(16*1024m writeSize);
    size_t nextWrite = LZ4F_compressBound(this->filter.writeSize, &this->preferences);
    size_t nextRead = passThroughSize(this, nextWrite);

}


FilterInterface lz4Interface = {
        .fnOpen = (FilterOpen)lz4FilterOpen,
        .fnRead = (FilterRead)lz4DecompressRead,
        .fnWrite = (FilterWrite)lz4CompressWrite,
        .fnClose = (FilterClose)lz4FilterClose,
        .fnSize = (FilterSize)lz4FilterSize
};
