/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/buffered.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "split; mkdir -p " TEST_DIR "split");

    beginTestGroup("File Splitting");
    FileSource *split =
            fileSourceNew(
                    blockifyNew(1024,
                            fileSplitFilterNew(1024 * 1024, formatPath, "%s-%06d.seg",
                                               fileSystemSinkNew())));
    seekTest(split, TEST_DIR "split/testfile_%u_%u");

    //splitVerify("Split into multiple files: verify files");
}
