cmake_minimum_required(VERSION 3.23)
project(filter C)

set(CMAKE_C_STANDARD 99)
include_directories(. /opt/local/include)

add_executable(filterTest filter/filterTest.c filter/filter.c filter/filter.h
        filter/passThrough.c filter/passThrough.h filter/bufferFilter.c filter/bufferFilter.h
        filter/request.c filter/request.h filter/fileSystemSink.c filter/fileSystemSink.h filter/buffer.c
        filter/buffer.h filter/error.h filter/fileSource.h filter/fileSource.c compress/lz4Compress.c compress/lz4Compress.h compress/lz4Decompress.c compress/lz4Decompress.h)
target_link_directories(filterTest PUBLIC /opt/local/lib)
target_link_libraries(filterTest PUBLIC lz4)
