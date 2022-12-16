/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/fileSource.h"
#include "file/fileSystemSink.h"
#include "file/buffered.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");
    FileSource *stream = fileSourceNew(blockifyNew(1024,fileSystemSinkNew(1)));

    singleStreamTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat", 64, 64);

    streamTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.

   
}
