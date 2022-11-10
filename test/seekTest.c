/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferSeek.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/streamTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "seek; mkdir -p " TEST_DIR "seek");

    beginTestGroup("Buffered Files");

    // Create a test file.
    size_t fileSize = 1048576;
    size_t blockSize = 1024;

    FileSource *seek = fileSourceNew(bufferSeekNew(blockSize, fileSystemSinkNew(blockSize)));
    singleStreamTest(seek, TEST_DIR "seek/testfile_%u_%u.dat", fileSize, blockSize);

    // Do pseudo-random seeks.
    verifyRandomFile(seek, TEST_DIR "seek/testfile_%u_%u.dat", fileSize, blockSize);

}
