cmake_minimum_required(VERSION 3.23)
project(filter C)

set(CMAKE_C_STANDARD 99)
include_directories(. /opt/local/include)

add_executable(filterTest test/filterTest.c common/filter.c common/filter.h
        common/passThrough.c common/passThrough.h file/bufferFilter.c file/bufferFilter.h
        common/request.c common/request.h file/fileSystemSink.c file/fileSystemSink.h common/buffer.c
        common/buffer.h common/error.h file/fileSource.h file/fileSource.c compress/lz4/lz4Compress.c compress/lz4/lz4Compress.h compress/lz4/lz4Decompress.c compress/lz4/lz4Decompress.h common/syscall.c common/syscall.h)
target_link_directories(filterTest PUBLIC /opt/local/lib)
target_link_libraries(filterTest PUBLIC lz4)
