/*
 * Header file for users of I/O stacks.
*/

#ifndef FILTER_IoStack_H
#define FILTER_IoStack_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/syslimits.h>

typedef uint8_t Byte;
typedef struct IoStack IoStack;

/*
 * Abstract functions required for each filter in an I/O Stack. TODO: declare as inline functions.
 */
#define fileOpen(this, path, oflags, mode)       (fileClearError(this), invoke(Open, this, path, oflags, mode))
#define fileWrite(this, buf, size, offset, ctx)  invoke(Write, this, buf, size, offset, (void *)ctx)
#define fileRead(this, buf, size, offset, ctx)   invoke(Read,  this, buf, size, offset, (void *)ctx)
#define fileSync(this, ctx)                      invoke(Sync, this, (void *)ctx)
#define fileSize(this)                           invokeNoParms(Size, this)
#define fileTruncate(this, offset)               invoke(Truncate, this, offset)
#define fileClose(this)                          invokeNoParms(Close, this)

/*
 * Universal functions - across all I/O stacks.
 * TODO: Should "this" be void* or IoStack* ?  Leaning towards IoStack ...
 */
ssize_t fileWriteAll(IoStack *this, const Byte *buf, size_t size, off_t offset, void *ctx);
ssize_t fileReadAll(IoStack *this, Byte *buf, size_t size, off_t offset, void *ctx);
ssize_t fileReadSized(IoStack *this, Byte *buf, size_t size, off_t offset, void *ctx);
ssize_t fileWriteSized(IoStack *this, const Byte *buf, size_t size, off_t offset, void *ctx);


bool filePrintf(IoStack *this, const char *format, ...);
bool fileScanf(IoStack *this, const char *format, ...);
bool fileError(void *thisVoid);
bool fileEof(void *thisVoid);
void fileClearError(void *thisVoid);
bool fileErrorInfo(void *thisVoid, int *errNo, char *errMsg);

/* read/write integers in network byte order (big endian) */
bool filePut1(void *this, uint8_t value);
bool filePut2(void *this, uint16_t value);
bool filePut4(void *this, uint32_t value);
bool filePut8(void *this, uint64_t value);
uint8_t fileGet1(void *this);
uint16_t fileGet2(void *this);
uint32_t fileGet4(void *this);
uint64_t fileGet8(void *this);

/* Release memory for I/O stack (after close) */
void freeIoStack(IoStack *ioStack);

/* Seek to the end of file */
#define FILE_END_POSITION END

/*
 * Additional filters provided by IoStack. Mix and match.
 */
typedef struct IoStackInterface IoStackInterface;

/* Universal header - minimum for an IoStack */

/* Filter for buffering data.  blockSize specifies the minimum size */
IoStack *bufferedNew(size_t bufferSize, void *next);
IoStack *lz4CompressNew(size_t blockSize, void *next);
IoStack *aeadNew(char *cipherName, size_t blockSize, Byte *key, size_t keyLen, void *next);

/* Bottom filter for talking to Posix files */
IoStack *fileSystemBottomNew();

/* We need some error code to signal an I/O Stack error. Pick an unlikely one as a filler */
#define EIOSTACK EBADF

/*
 * Internals moved here so fileRead, fileWrite, etc wrappers can be inlined.
 */
#define invoke(call, stack, ...)   (((IoStack *)stack)->iface->fn##call((void*)stack, __VA_ARGS__))
#define invokeNoParms(call, stack) (((IoStack *)stack)->iface->fn##call((void*)stack))


struct IoStack
{
	IoStack *next;
	IoStackInterface *iface;
	size_t blockSize;
	bool eof;
	int errNo;
	char errMsg[121];   /* alloc? */
};

/*
 * A set of functions each IoStack must provide.
 */
typedef bool (*IoStackOpen)(void *this, const char *path, int mode, int perm);
typedef ssize_t (*IoStackRead)(void *this, Byte *buf, size_t size, off_t offset, void *ctx);
typedef ssize_t (*IoStackWrite)(void *this, const Byte *buf, size_t size, off_t offset, void *ctx);
typedef bool (*IoStackSync)(void *this, void *ctx);
typedef bool (*IoStackClose)(void *this);
typedef off_t (*IoStackSize)(void *this);
typedef bool (*IoStackTruncate) (void *this, off_t offset);

struct IoStackInterface {
	IoStackOpen fnOpen;
	IoStackWrite fnWrite;
	IoStackClose fnClose;
	IoStackRead fnRead;
	IoStackSync fnSync;
	IoStackSize fnSize;
	IoStackTruncate fnTruncate;
};


#endif /*FILTER_IoStack_H */
