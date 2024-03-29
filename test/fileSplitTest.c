/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/buffered.h"
#include "file/fileSystemBottom.h"
#include "compress/lz4/lz4.h"
#include "iostack.h"
#include "fileSplit/fileSplit.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "split; mkdir -p " TEST_DIR "split");

    beginTestGroup("File Splitting");
    IoStack *split =
            ioStackNew(
                    bufferedNew(1024,
                            fileSplitNew(1024 * 1024, formatPath, "%s-%06d.seg",
                                               fileSystemBottomNew())));
    seekTest(split, TEST_DIR "split/testfile_%u_%u");

    //splitVerify("Split into multiple files: verify files");
}
