/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "../src/iostack.h"
#include "./framework/fileFramework.h"
#include "./framework/unitTest.h"

IoStack *createStack(size_t blockSize)
{
	return fileSystemBottomNew();
}

void testMain()
{
    system("rm -rf " TEST_DIR "raw; mkdir -p " TEST_DIR "raw");

    beginTestGroup("Raw Files");
    seekTest(createStack, TEST_DIR "raw/testfile_%u_%u.dat");
}
