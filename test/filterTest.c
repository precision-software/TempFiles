/*  */
#include <stdio.h>
#include <sys/fcntl.h>
#include "file/bufferStream.h"
#include "file/fileSystemSink.h"
#include "compress/lz4/lz4.h"
#include "file/fileSource.h"
#include "fileSplit/fileSplit.h"

#include "framework/streamTest.h"


void testMain()
{
    system("rm -rf " TEST_DIR "buffered; mkdir -p " TEST_DIR "buffered");
    system("rm -rf " TEST_DIR "compressed; mkdir -p " TEST_DIR "compressed");
    system("rm -rf " TEST_DIR "split; mkdir -p " TEST_DIR "split");

    beginTestGroup("Buffered Files");
    FileSource *stream = fileSourceNew(bufferStreamNew(fileSystemSinkNew()));
    streamTest(stream, TEST_DIR "buffered/testfile_%u_%u.dat");

    // open/close/read/write errors.

    beginTestGroup("LZ4 Compression");
    FileSource *lz4 = fileSourceNew(lz4FilterNew(bufferStreamNew(fileSystemSinkNew()), 16 * 1024));
    //streamTest(lz4, TEST_DIR "compressed/testfile_%u_%u.lz4");
    //compressionVerify("LZ4 Compression: verify with external utility");

    beginTestGroup("File Splitting");
    FileSource *split =
            fileSourceNew(
                    bufferStreamNew(
                            fileSplitFilterNew(
                                    fileSystemSinkNew(),
                                    1024 * 1024,
                                    formatPath, "%s-%06d.seg")));
    streamTest(split, TEST_DIR "split/testfile_%u_%u");
    //splitVerify("Split into multiple files: verify files");
}
