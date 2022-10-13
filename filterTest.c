/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "bufferFilter.h"
#include "fileSystemSink.h"
#include "fileSource.h"

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

    return 0;
}
