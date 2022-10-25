cmake_minimum_required(VERSION 3.23)
project(filter C)

set(CMAKE_C_STANDARD 99)
include_directories(src /opt/local/include test)

add_executable(filterTest test/filterTest.c src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h src/file/bufferStream.c src/file/bufferStream.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h src/common/buffer.c
        src/common/buffer.h src/common/error.h src/file/fileSource.h src/file/fileSource.c src/compress/lz4/lz4Compress.c
        src/compress/lz4/lz4Decompress.c src/common/syscall.c src/common/syscall.h src/compress/lz4/lz4.c
        src/compress/lz4/lz4.h src/compress/lz4/lz4_internal.h src/fileSplit/fileSplit.c src/fileSplit/fileSplit.h
        src/common/convertFilter.c src/common/convertFilter.h test/framework/unitTestInternal.h test/framework/streamTest.c
        test/framework/streamTest.h test/framework/unitTest.h
        src/common/converter.h)

target_link_directories(filterTest PUBLIC /opt/local/lib /opt/homebrew/Cellar/openssl@1.1/1.1.1q/lib)
target_link_libraries(filterTest PUBLIC lz4)

add_executable(encryptTest test/encryptionTest.c src/common/filter.c src/common/filter.h
        src/common/passThrough.c src/common/passThrough.h src/file/bufferStream.c src/file/bufferStream.h
        src/file/fileSystemSink.c src/file/fileSystemSink.h src/common/buffer.c
        src/common/buffer.h src/common/error.h src/file/fileSource.h src/file/fileSource.c src/compress/lz4/lz4Compress.c
        src/compress/lz4/lz4Decompress.c src/common/syscall.c src/common/syscall.h src/compress/lz4/lz4.c
        src/compress/lz4/lz4.h src/compress/lz4/lz4_internal.h
        src/common/convertFilter.c src/common/convertFilter.h test/framework/unitTestInternal.h test/framework/streamTest.c
        test/framework/streamTest.h test/framework/unitTest.h src/encrypt/openSSL/openSSL.c src/encrypt/openSSL/openSSL.h
        src/common/converter.h src/common/pipeline.c src/common/pipeline.h)

target_link_directories(encryptTest PUBLIC /opt/local/lib /opt/homebrew/Cellar/openssl@1.1/1.1.1q/lib)
target_link_libraries(encryptTest PUBLIC lz4 crypto)
