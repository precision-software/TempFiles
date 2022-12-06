/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/fileSource.h"
#include "file/fileSystemSink.h"

#include "framework/streamFramework.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");
    FileSource *stream = fileSourceNew(fileSystemSinkNew(4096));
    streamTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.


}
