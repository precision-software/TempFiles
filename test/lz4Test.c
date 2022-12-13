/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "common/filter.h"
#include "common/error.h"
#include "file/fileSystemSink.h"
#include "file/buffered.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"

#include "framework/streamFramework.h"


void testMain()
{
    system("rm -rf " TEST_DIR "compressed; mkdir -p " TEST_DIR "compressed");

    beginTestGroup("LZ4 Compression");
    FileSource *lz4 =
            fileSourceNew(blockifyNew(1024,
                lz4CompressNew(1024,
                    fileSystemSinkNew(1))));

    singleStreamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4", 1024, 1024);
    streamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4");

}
