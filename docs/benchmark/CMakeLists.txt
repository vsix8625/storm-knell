cmake_minimum_required(VERSION 3.20)
project(lua LANGUAGES C VERSION 5.4.7)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_COMPILER clang)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_executable(lua
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c
    lua.c
)

target_compile_definitions(lua PRIVATE
    NDEBUG
    LUA_USE_LINUX
    LUA_USE_READLINE
)

target_compile_options(lua PRIVATE
    -std=c17
    -Wall
    -Wextra
    -Werror
    -O3
    -march=native
)

target_link_options(lua PRIVATE
    -Wl,-O1
    -Wl,--as-needed
)

target_link_libraries(lua PRIVATE
    readline
    m
    dl
)

set_target_properties(lua PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
)
