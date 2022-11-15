/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/streamFramework.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");
    FileSource *stream = fileSourceNew(bufferStreamNew(fileSystemSinkNew(2048)));
    streamTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.

   
}
