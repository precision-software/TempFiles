/*  */
#include <stdio.h>
#include <sys/fcntl.h>

#include "encrypt/openSSL/openSSL.h"
#include "fileSplit/fileSplit.h"
#include "compress/lz4/lz4.h"

#include "file/fileSource.h"
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"

#include "framework/streamFramework.h"


void testMain()
{
    system("rm -rf " TEST_DIR "kitchen; mkdir -p " TEST_DIR "kitchen");

    beginTestGroup("Kitchen Sink");
    FileSource *stream =
        fileSourceNew(
            bufferStreamNew(
                lz4FilterNew(16*1024,
                    openSSLNew(
                        "AES-256-CBC",
                        (Byte *)"0123456789ABCDEF0123456789ABCDEF", 32,
                        (Byte *)"FEDCBA9876543210FEDCBA9876543210", 32,
                        fileSplitFilterNew(1024 * 1024, formatPath, "%s-%06d.seg",
                            fileSystemSinkNew(0))))));

    streamTest(stream, TEST_DIR "kitchen/testfile_%u_%u.dat");
}
