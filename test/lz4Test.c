/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/streamTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "compressed; mkdir -p " TEST_DIR "compressed");

    beginTestGroup("LZ4 Compression");
    FileSource *lz4 = fileSourceNew(
            lz4FilterNew(16*1024,
                    bufferStreamNew(
                            fileSystemSinkNew())));
    streamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4");
    //compressionVerify("LZ4 Compression: verify with external utility");
}
