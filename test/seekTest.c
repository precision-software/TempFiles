/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/buffered.h"
#include "file/fileSystemSink.h"
#include "file/fileSource.h"

#include "framework/unitTest.h"
#include "framework/seekFramework.h"



void testMain()
{
    system("rm -rf " TEST_DIR "seek; mkdir -p " TEST_DIR "seek");
    char name[] = TEST_DIR "seek/testfile_%u_%u.dat";

    beginTestGroup("Buffered Seeks");

    FileSource *seek = fileSourceNew(blockifyNew(1024, fileSystemSinkNew(1024)));

    seekTest(seek, name);
}
