/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/buffered.h"
#include "file/fileSystemSink.h"
#include "encrypt/libcrypto/aead.h"
#include "file/fileSource.h"

#include "framework/seekFramework.h"
#include "framework/unitTest.h"

void testMain()
{
    system("rm -rf " TEST_DIR "encryption; mkdir -p " TEST_DIR "encryption");

    beginTestGroup("AES Encrypted Files - Seek");
    FileSource *stream =
            fileSourceNew(
                    blockifyNew(4*1024,
                                aeadFilterNew("AES-256-GCM", 1024, (Byte *)"0123456789ABCDEF0123456789ABCDEF", 32,
                                              fileSystemSinkNew(0))));

    //singleSeekTest(stream, TEST_DIR "encryption/testfile_%u_%u.dat", 1024, 64);
    seekTest(stream, TEST_DIR "encryption/testfile_%u_%u.dat");
}
