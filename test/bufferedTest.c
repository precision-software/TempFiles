/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/ioStack.h"
#include "file/fileSystemBottom.h"
#include "file/buffered.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");
    IoStack *stream = ioStackNew(bufferedNew(1024,fileSystemBottomNew()));

    singleSeekTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat", 64, 64);

    seekTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.

   
}
