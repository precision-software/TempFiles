/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#include "../src/iostack.h"
#include "./framework/fileFramework.h"
#include "./framework/unitTest.h"

IoStack *createStack(size_t blockSize)
{
	return bufferedNew(1,
			 aeadNew("AES-256-GCM", blockSize, (Byte *) "0123456789ABCDEF0123456789ABCDEF", 32,
				   fileSystemBottomNew()));
}

void testMain()
{
    system("rm -rf " TEST_DIR "encryption; mkdir -p " TEST_DIR "encryption");

    beginTestGroup("AES Encrypted Files");

    singleSeekTest(createStack, TEST_DIR "encryption/testfile_%u_%u.dat", 64, 1024);
    seekTest(createStack, TEST_DIR "encryption/testfile_%u_%u.dat");
}
