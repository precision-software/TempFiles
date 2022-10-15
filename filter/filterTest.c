/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "filter/bufferFilter.h"
#include "filter/fileSystemSink.h"
#include "compress/lz4Compress.h"
#include "compress/lz4Decompress.h"
#include "filter/fileSource.h"

int main() {

    FileSource *stream = fileSourceNew(bufferFilterNew( fileSystemSinkNew() ));

    Error error = fileOpen(stream, "/tmp/testFile", O_WRONLY|O_CREAT|O_TRUNC, 0666);
    size_t count = fileWrite(stream, (Byte*)"Hello World!\n", 13, &error);
    fileClose(stream, &error);

    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);

    Byte buf[1024];
    error = fileOpen(stream, "/tmp/testFile", O_RDONLY, 0666);
    count = fileRead(stream, buf, sizeof(buf), &error);
    fileClose(stream, &error);

    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);

    buf[13] = '\0';
    printf("Read back: %s", buf);

    FileSource *compressed = fileSourceNew(lz4CompressFilterNew(bufferFilterNew( fileSystemSinkNew() ), 16*1024));
    error = fileOpen(compressed, "/tmp/testFile.lz4", O_WRONLY|O_CREAT|O_TRUNC, 0666);
    count = fileWrite(compressed, (Byte*)"Hello World!\n", 13, &error);
    fileClose(compressed, &error);
    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);

    FileSource *decompress = fileSourceNew(lz4DecompressFilterNew(bufferFilterNew (fileSystemSinkNew()), 16*1024));
    error = fileOpen(decompress, "/tmp/testFile.lz4", O_RDONLY, 0666);
    count = fileRead(decompress, buf, sizeof(buf), &error);
    fileClose(decompress, &error);
    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);
    buf[13] = '\0';
    printf("Read back: %s", buf);

    return 0;
}
// TODO: Make it OK to clase an already closed pipeline.
