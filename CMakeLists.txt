
project(IoStack C)

cmake_minimum_required(VERSION 3.23)
set(CMAKE_C_STANDARD 99)

find_package(OpenSSL)
#find_package(lz4)


# Build a library consisting of all sources under "src"
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_SOURCE_DIR} "src/*.c")
add_library(iostack STATIC ${SOURCES})
set_target_properties(iostack PROPERTIES POSITION_INDEPENDENT_CODE on)

target_include_directories(iostack
        PUBLIC
        /opt/local/include
        )

install(TARGETS iostack
        ARCHIVE DESTINATION "/opt/local/lib"
        )

install(FILES
        src/iostack.h
        src/iostack_error.h
        DESTINATION "/opt/local/include"
        )

install(FILES
        iostackConfig.cmake
        DESTINATION "/opt/local/lib/cmake/iostack" )


# Build test programs
include_directories(src test)
link_directories(/opt/local/lib)
link_libraries(iostack crypto  lz4)

add_executable(rawTest test/rawTest.c test/framework/fileFramework.c)
add_executable(bufferedTest test/bufferedTest.c test/framework/fileFramework.c)
add_executable(fileSplitTest test/fileSplitTest.c test/framework/fileFramework.c)
add_executable(aeadTest test/aeadTest.c test/framework/fileFramework.c)
add_executable(lz4Test test/lz4Test.c test/framework/fileFramework.c)
add_executable(kitchenSinkTest test/kitchenSinkTest.c test/framework/fileFramework.c)
