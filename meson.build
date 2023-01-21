project(IoStack C)

cmake_minimum_required(VERSION 3.23)
set(CMAKE_C_STANDARD 99)

find_package(OpenSSL)
#find_library(lz4)

# Build a library consisting of all sources under "src"
file(GLOB_RECURSE SOURCES  RELATIVE ${CMAKE_SOURCE_DIR} "src/*.c")

add_library(iostack STATIC ${SOURCES})
# set_target_properties(iostack PROPERTIES POSITION_INDEPENDENT_CODE on)

install(TARGETS iostack
        ARCHIVE DESTINATION "/opt/local/lib"
        )

install(DIRECTORY src/ DESTINATION include/iostack
        FILES_MATCHING PATTERN "*.h")

install(FILES
        iostack.pc
        DESTINATION "lib/pkg-config/iostack" )


# Build test programs
include_directories(src test  /usr/local/include/iostack /usr/local/include /opt/local/include /opt/homebrew/include)
link_directories(/opt/local/lib /usr/local/lib /opt/homebrew/lib)
link_libraries( crypto  lz4 iostack )

add_executable(rawTest test/rawTest.c test/framework/fileFramework.c)
add_executable(bufferedTest test/bufferedTest.c test/framework/fileFramework.c)
add_executable(fileSplitTest test/fileSplitTest.c test/framework/fileFramework.c)
add_executable(aeadTest test/aeadTest.c test/framework/fileFramework.c)
add_executable(lz4Test test/lz4Test.c test/framework/fileFramework.c)
add_executable(kitchenSinkTest test/kitchenSinkTest.c test/framework/fileFramework.c)
