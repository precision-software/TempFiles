cmake_minimum_required(VERSION 3.23)
project(filter C)

set(CMAKE_C_STANDARD 23)

add_executable(filterTest filterTest.c filter.c filter.h
        passThrough.c passThrough.h bufferFilter.c bufferFilter.h
        request.c request.h fileSystemSink.c fileSystemSink.h buffer.c buffer.h error.h fileSource.h fileSource.c)
