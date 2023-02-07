/*  */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#include "../src/iostack.h"
#include "framework/fileFramework.h"
#include "framework/unitTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");

    beginTestGroup("Buffered Files");
    IoStack *iostack = bufferedNew(1024,fileSystemBottomNew());

    singleSeekTest(iostack, TEST_DIR "buffered/testfile_%u_%u.dat", 1059, 35);

    seekTest(iostack, TEST_DIR "buffered/testfile_%u_%u.dat");


   
}
