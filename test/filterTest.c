/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"

/* A very quick test program - only prints out "Hello World". Obviously more extensive tests to come. */
int main() {

    FileSource *stream = fileSourceNew(bufferStreamNew( fileSystemSinkNew() ));

    Error error = fileOpen(stream, "/tmp/testFile", O_WRONLY|O_CREAT|O_TRUNC, 0666);
    size_t count = fileWrite(stream, (Byte*)"Hello World!\n", 13, &error);
    fileClose(stream, &error);

    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);

    Byte buf[1024]; strcpy((char*)buf, "Buffer data not read in yet");
    error = fileOpen(stream, "/tmp/testFile", O_RDONLY, 0666);
    count = fileRead(stream, buf, sizeof(buf), &error);
    fileClose(stream, &error);

    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);

    buf[13] = '\0';
    printf("Read back: %s", buf);

    FileSource *lz4 = fileSourceNew(lz4FilterNew(bufferStreamNew( fileSystemSinkNew() ), 16*1024));
    error = fileOpen(lz4, "/tmp/testFile.lz4", O_WRONLY|O_CREAT|O_TRUNC, 0666);
    count = fileWrite(lz4, (Byte*)"Hello World!\n", 13, &error);
    fileClose(lz4, &error);
    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);

    strcpy((char*)buf, "Buffer data not read in yet");
    error = fileOpen(lz4, "/tmp/testFile.lz4", O_RDONLY, 0666);
    count = fileRead(lz4, buf, sizeof(buf), &error);
    fileClose(lz4, &error);
    if (!errorIsOK(error))
        printf("The error msg: (%d) %s\n", error.code, error.msg);
    buf[13] = '\0';
    printf("Read back: %s", buf);

    return 0;
}
// TODO: Make it OK to clase an already closed pipeline.
