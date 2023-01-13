/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "common/filter.h"
#include "common/error.h"
#include "file/fileSystemBottom.h"
#include "file/buffered.h"
#include "compress/lz4/lz4.h"
#include "file/ioStack.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "compressed; mkdir -p " TEST_DIR "compressed");

    beginTestGroup("LZ4 Compression");
    IoStack *lz4 =
            ioStackNew(
                bufferedNew(1024,
                    lz4CompressNew(1024,
                        bufferedNew(1024,
                            fileSystemBottomNew()))));

    singleReadSeekTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4", 1024, 64);
    readSeekTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4");

}
