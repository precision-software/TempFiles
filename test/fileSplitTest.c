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
    system("rm -rf " TEST_DIR "split; mkdir -p " TEST_DIR "split");

    beginTestGroup("File Splitting");
    FileSource *split =
            fileSourceNew(
                    bufferStreamNew(
                            fileSplitFilterNew(1024 * 1024,formatPath, "%s-%06d.seg",
                                               fileSystemSinkNew(0))));
    streamTest(split, TEST_DIR "split/testfile_%u_%u");

    //splitVerify("Split into multiple files: verify files");
}
