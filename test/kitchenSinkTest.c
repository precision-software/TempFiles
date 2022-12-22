/*  */
#include <stdio.h>
#include <sys/fcntl.h>

#include "encrypt/libcrypto/aead.h"
#include "fileSplit/fileSplit.h"
#include "compress/lz4/lz4.h"

#include "file/fileSource.h"
#include "file/buffered.h"
#include "file/fileSystemSink.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "kitchen; mkdir -p " TEST_DIR "kitchen");

    beginTestGroup("Kitchen Sink");
    FileSource *stream =
        fileSourceNew(
            blockifyNew(16*1024,
                lz4CompressNew(16*1024,
                    blockifyNew(1024,
                        aeadFilterNew("AES-256-GCM", 1024, (Byte *)"0123456789ABCDEF0123456789ABCDEF", 32,
                            fileSplitFilterNew(16 * 1024, formatPath, "%s-%06d.seg",
                                fileSystemSinkNew()))))));

    //singleStreamTest(stream, TEST_DIR "kitchen/testfile_%u_%u.dat", 0, 1024);
    readSeekTest(stream, TEST_DIR "kitchen/testfile_%u_%u.dat");
}
