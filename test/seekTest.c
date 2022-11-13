/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferSeek.h"
#include "file/fileSystemSink.h"
#include "file/fileSource.h"

#include "framework/unitFramework.h"
#include "framework/seekFramework.h"



void testMain()
{
    system("rm -rf " TEST_DIR "seek; mkdir -p " TEST_DIR "seek");
    char name[] = TEST_DIR "seek/testfile_%u_%u.dat";

    beginTestGroup("Buffered Seeks");

    FileSource *seek = fileSourceNew(bufferSeekNew(1024, fileSystemSinkNew(1024)));

    singleSeekTest(seek, name, 1024*1024, 2037);
    seekTest(seek, name);
}
