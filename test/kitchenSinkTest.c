/*  */
#include <stdio.h>
#include <sys/fcntl.h>

#include "../src/iostack.h"
#include "./framework/fileFramework.h"
#include "./framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "kitchen; mkdir -p " TEST_DIR "kitchen");

    beginTestGroup("Kitchen Sink");
    IoStack *stream =
        ioStackNew(
			bufferedNew(16*1024,
                lz4CompressNew(16*1024,
                    bufferedNew(1024,
                        aeadFilterNew("AES-256-GCM", 1024, (Byte *)"0123456789ABCDEF0123456789ABCDEF", 32,
                            fileSplitNew(2 * 1024, formatPath, "%s-%06d.seg",
                                fileSystemBottomNew()))))));

    readSeekTest(stream, TEST_DIR "kitchen/testfile_%u_%u.dat");
}
