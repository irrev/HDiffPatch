﻿# CMakeList.txt : CMake project for HDiffPatch, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
#if (POLICY CMP0141)
#  cmake_policy(SET CMP0141 NEW)
#  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
#endif()


project ("HDiffPatch")

# Add source to this project's executable.
#add_executable (HDiffPatch "HDiffPatch.cpp" "HDiffPatch.h")

#if (CMAKE_VERSION VERSION_GREATER 3.12)
#  set_property(TARGET HDiffPatch PROPERTY CXX_STANDARD 20)
#endif()

# TODO: Add tests and install targets if needed.


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 搜集库文件
file(GLOB_RECURSE libHDiffPatch "../libHDiffPatch/*")
file(GLOB_RECURSE libParallel "../libParallel/*")

# 搜集源文件
file(GLOB_RECURSE SOURCES "HDiffPatch.cpp" "HDiffPatch.h")

# 添加到库
add_library(HDiffPatch SHARED ${libHDiffPatch} ${libParallel} ${SOURCES})

# 设置库的输出目录
set_target_properties(HDiffPatch PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 为不同平台添加平台宏定义
if(WIN32)
    target_compile_definitions(HDiffPatch PRIVATE HDIFFPATCH_PLATFORM_WINDOWS)
elseif(ANDROID)
    target_compile_definitions(HDiffPatch PRIVATE HDIFFPATCH_PLATFORM_ANDROID)
elseif(IOS)
    target_compile_definitions(HDiffPatch PRIVATE HDIFFPATCH_PLATFORM_IOS)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(HDiffPatch PRIVATE HDIFFPATCH_PLATFORM_LINUX)
elseif(APPLE)
    target_compile_definitions(HDiffPatch PRIVATE HDIFFPATCH_PLATFORM_MACOS)
endif()


# 添加导出宏
target_compile_definitions(HDiffPatch PRIVATE HDIFFPATCH_EXPORTS)