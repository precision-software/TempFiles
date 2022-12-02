/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/byteStream.h"
#include "file/fileSystemSink.h"
#include "encrypt/openSSL/encrypt.h"
#include "file/fileSource.h"

#include "framework/streamFramework.h"


void testMain()
{
    system("rm -rf " TEST_DIR "encryption; mkdir -p " TEST_DIR "encryption");

    beginTestGroup("AES Encrypted Files");
    FileSource *stream =
        fileSourceNew(
            bufferStreamNew(
                openSSLNew(
                    "AES-256-CBC",
                    (Byte *)"0123456789ABCDEF0123456789ABCDEF", 32,
                    (Byte *)"FEDCBA9876543210FEDCBA9876543210", 32,
                    fileSystemSinkNew(0))));

    streamTest(stream, TEST_DIR "encryption/testfile_%u_%u.dat");
}
