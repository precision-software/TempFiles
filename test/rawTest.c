/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/fileSource.h"
#include "file/fileSystemSink.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"

void testMain()
{
    system("rm -rf " TEST_DIR "raw; mkdir -p " TEST_DIR "raw");

    beginTestGroup("Raw Files");
    FileSource *stream = fileSourceNew(fileSystemSinkNew(1));
    seekTest(stream, TEST_DIR "raw/testfile_%u_%u.dat");

    // open/close/read/write errors.


}
