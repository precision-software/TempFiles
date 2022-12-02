/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/byteStream.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/streamFramework.h"


void testMain()
{
    system("rm -rf " TEST_DIR "compressed; mkdir -p " TEST_DIR "compressed");

    beginTestGroup("LZ4 Compression");
    FileSource *lz4 = fileSourceNew(
            lz4FilterNew(16*1024,
                    bufferStreamNew(
                            fileSystemSinkNew(0))));

    singleStreamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4", 1048576, 1024);

    streamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4");
    //compressionVerify("LZ4 Compression: verify with external utility");
}
