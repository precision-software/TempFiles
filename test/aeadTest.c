/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include "file/buffered.h"
#include "file/fileSystemBottom.h"
#include "encrypt/libcrypto/aead.h"
#include "iostack.h"

#include "framework/fileFramework.h"
#include "framework/unitTest.h"

void testMain()
{
    system("rm -rf " TEST_DIR "encryption; mkdir -p " TEST_DIR "encryption");

    beginTestGroup("AES Encrypted Files");
    IoStack *stream =
        ioStackNew(
            bufferedNew(1024,
                aeadFilterNew("AES-256-GCM", 1024, (Byte *)"0123456789ABCDEF0123456789ABCDEF", 32,
                    fileSystemBottomNew())));

    //singleSeekTest(stream, TEST_DIR "encryption/testfile_%u_%u.dat", 64, 1024);
    seekTest(stream, TEST_DIR "encryption/testfile_%u_%u.dat");
}
