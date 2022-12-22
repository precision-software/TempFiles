cmake_minimum_required(VERSION 3.23)
project(filter C)

set(CMAKE_C_STANDARD 99)
include_directories(src /opt/local/include test)
link_directories(/opt/local/lib /opt/homebrew/Cellar/openssl@1.1/1.1.1s/lib)

add_executable(rawTest test/rawTest.c
        test/framework/unitTestInternal.h test/framework/fileFramework.c test/framework/fileFramework.h test/framework/unitTest.h
        src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h
        src/common/error.h src/file/fileSource.h src/file/fileSource.c
        src/common/syscall.c src/common/syscall.h)



add_executable(blockifyTest test/blockifyTest.c
        src/file/buffered.c src/file/buffered.h
        test/framework/unitTestInternal.h test/framework/fileFramework.c test/framework/fileFramework.h test/framework/unitTest.h
        src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h
        src/common/error.h src/file/fileSource.h src/file/fileSource.c
        src/common/syscall.c src/common/syscall.h)


add_executable(aeadTest test/aeadTest.c
        src/encrypt/libcrypto/aead.c src/encrypt/libcrypto/aead.h
        test/framework/unitTestInternal.h test/framework/fileFramework.c test/framework/fileFramework.h test/framework/unitTest.h
        src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h
        src/common/error.h src/file/fileSource.h src/file/fileSource.c
        src/common/syscall.c src/common/syscall.h
        src/file/buffered.c src/file/buffered.h)
target_link_libraries(aeadTest crypto)

add_executable(aeadSeekTest test/aeadSeekTest.c
        src/encrypt/libcrypto/aead.c src/encrypt/libcrypto/aead.h
        test/framework/unitTestInternal.h test/framework/fileFramework.c test/framework/fileFramework.h test/framework/unitTest.h
        src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h
        src/common/error.h src/file/fileSource.h src/file/fileSource.c
        src/common/syscall.c src/common/syscall.h
        src/file/buffered.c src/file/buffered.h)
target_link_libraries(aeadSeekTest crypto)


add_executable(lz4Test test/lz4Test.c
        src/compress/lz4/lz4.c  src/compress/lz4/lz4.h  src/file/buffered.h src/file/buffered.c
        test/framework/unitTestInternal.h test/framework/fileFramework.c test/framework/fileFramework.h test/framework/unitTest.h
        src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h
        src/common/error.h src/file/fileSource.h src/file/fileSource.c
        src/common/syscall.c src/common/syscall.h
        src/file/buffered.c src/file/buffered.h)
target_link_libraries(lz4Test lz4)


add_executable(fileSplitTest test/fileSplitTest.c
        src/fileSplit/fileSplit.c src/fileSplit/fileSplit.h
        src/file/buffered.c src/file/buffered.h
        test/framework/unitTestInternal.h test/framework/fileFramework.c test/framework/fileFramework.h test/framework/unitTest.h
        src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h
        src/common/error.h src/file/fileSource.h src/file/fileSource.c
        src/common/syscall.c src/common/syscall.h)
