cmake_minimum_required(VERSION 3.8)
project(HDiffPatchTestRunner)

# Disable all plugins to avoid external dependencies
option(HDIFFPATCH_USE_ZLIB "Enable zlib" OFF)
option(HDIFFPATCH_USE_BZIP2 "Enable bzip2" OFF)
option(HDIFFPATCH_USE_LZMA "Enable lzma" OFF)
option(HDIFFPATCH_USE_ZSTD "Enable zstd" OFF)
option(HDIFFPATCH_USE_LDEF "Enable libdeflate" OFF)
option(HDIFFPATCH_USE_MD5 "Enable md5" OFF)
option(HDIFFPATCH_USE_XXH "Enable xxHash" OFF)
option(HDIFFPATCH_MT "Enable multithreading" OFF)

# Include the library build configuration
add_subdirectory(../build_libs build_libs)

# Create the test executable
add_executable(hdiffpatch_test hdiffpatch_test.cpp)

# Link against the static library
target_link_libraries(hdiffpatch_test PRIVATE HDiffPatchStatic)

# Include directories
target_include_directories(hdiffpatch_test PRIVATE ../build_libs/include)

# Set output directory for the executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
