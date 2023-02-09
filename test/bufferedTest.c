/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#include "../src/iostack.h"
#include "framework/fileFramework.h"
#include "framework/unitTest.h"


/* We'll use a fixed block size, but vary the size of the writes */
IoStack *createStack(size_t bufferSize)
{
	return bufferedNew(1024, fileSystemBottomNew());
}

void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");

    singleSeekTest(createStack, TEST_DIR "buffered/testfile_%u_%u.dat", 1024, 64);

    seekTest(createStack, TEST_DIR "buffered/testfile_%u_%u.dat");
}
