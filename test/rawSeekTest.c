
/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/fileSource.h"
#include "file/fileSystemSink.h"

#include "framework/seekFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");
    FileSource *stream = fileSourceNew(fileSystemSinkNew(4096));
    seekTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.


}
