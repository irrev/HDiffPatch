# CMakeList.txt : CMake project for HDiffPatch, include source and define
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

# Options
option(HDIFFPATCH_USE_ZLIB "Enable zlib" ON)
option(HDIFFPATCH_USE_BZIP2 "Enable bzip2" OFF)
option(HDIFFPATCH_USE_LZMA "Enable lzma" ON)
option(HDIFFPATCH_USE_ZSTD "Enable zstd" ON)
option(HDIFFPATCH_USE_LDEF "Enable libdeflate" OFF)
option(HDIFFPATCH_USE_MD5 "Enable md5" OFF)
option(HDIFFPATCH_USE_XXH "Enable xxHash" OFF)
option(HDIFFPATCH_MT "Enable multithreading" OFF)

set(DEP_SOURCES "")
set(DEP_INCLUDES "")
set(DEP_DEFINITIONS "")

# Default Definitions
list(APPEND DEP_DEFINITIONS 
    "_IS_NEED_DIR_DIFF_PATCH=1" 
    "_ChecksumPlugin_fadler64"
    "_IS_NEED_BSDIFF=1"
    "_IS_NEED_VCDIFF=1"
)

if(HDIFFPATCH_MT)
    list(APPEND DEP_DEFINITIONS "_IS_USED_MULTITHREAD=1")
else()
    list(APPEND DEP_DEFINITIONS "_IS_USED_MULTITHREAD=0")
endif()

# ZLIB
if(HDIFFPATCH_USE_ZLIB)
    file(GLOB_RECURSE ZLIB_SRCS "../zlib/*.c")
    list(APPEND DEP_SOURCES ${ZLIB_SRCS})
    list(APPEND DEP_INCLUDES "../zlib")
    list(APPEND DEP_DEFINITIONS "_CompressPlugin_zlib" "_ChecksumPlugin_crc32")
endif()

# BZIP2
if(HDIFFPATCH_USE_BZIP2)
    file(GLOB_RECURSE BZIP2_SRCS "../bzip2/*.c")
    list(APPEND DEP_SOURCES ${BZIP2_SRCS})
    list(APPEND DEP_INCLUDES "../bzip2")
    list(APPEND DEP_DEFINITIONS "_CompressPlugin_bz2")
endif()

# LZMA
if(HDIFFPATCH_USE_LZMA)
    set(LZMA_DIR "../lzma/C")
    list(APPEND DEP_INCLUDES ${LZMA_DIR})
    list(APPEND DEP_DEFINITIONS "_CompressPlugin_lzma" "_CompressPlugin_lzma2" "_CompressPlugin_7zXZ")
    
    set(LZMA_SRCS 
        "${LZMA_DIR}/LzmaDec.c" "${LZMA_DIR}/Lzma2Dec.c" "${LZMA_DIR}/7zStream.c" 
        "${LZMA_DIR}/CpuArch.c" "${LZMA_DIR}/Alloc.c" "${LZMA_DIR}/LzFind.c" 
        "${LZMA_DIR}/LzFindOpt.c" "${LZMA_DIR}/LzmaEnc.c" "${LZMA_DIR}/Lzma2Enc.c"
        "${LZMA_DIR}/7zCrc.c" "${LZMA_DIR}/7zCrcOpt.c" "${LZMA_DIR}/Bra.c" 
        "${LZMA_DIR}/Bra86.c" "${LZMA_DIR}/BraIA64.c" "${LZMA_DIR}/Delta.c" 
        "${LZMA_DIR}/Sha256.c" "${LZMA_DIR}/Sha256Opt.c" "${LZMA_DIR}/Xz.c" 
        "${LZMA_DIR}/XzCrc64.c" "${LZMA_DIR}/XzCrc64Opt.c" "${LZMA_DIR}/XzDec.c" 
        "${LZMA_DIR}/XzEnc.c"
    )
    if(HDIFFPATCH_MT)
        list(APPEND LZMA_SRCS 
            "${LZMA_DIR}/MtDec.c" "${LZMA_DIR}/Threads.c" 
            "${LZMA_DIR}/LzFindMt.c" "${LZMA_DIR}/MtCoder.c"
        )
    endif()
    list(APPEND DEP_SOURCES ${LZMA_SRCS})
endif()

# ZSTD
if(HDIFFPATCH_USE_ZSTD)
    set(ZSTD_DIR "../zstd/lib")
    file(GLOB ZSTD_COMMON "${ZSTD_DIR}/common/*.c")
    file(GLOB ZSTD_COMPRESS "${ZSTD_DIR}/compress/*.c")
    file(GLOB ZSTD_DECOMPRESS "${ZSTD_DIR}/decompress/*.c")
    list(APPEND DEP_SOURCES ${ZSTD_COMMON} ${ZSTD_COMPRESS} ${ZSTD_DECOMPRESS})
    list(APPEND DEP_INCLUDES "${ZSTD_DIR}" "${ZSTD_DIR}/common" "${ZSTD_DIR}/compress" "${ZSTD_DIR}/decompress")
    list(APPEND DEP_DEFINITIONS 
        "_CompressPlugin_zstd" 
        "ZSTD_DISABLE_ASM=1" 
        "ZSTD_STRIP_ERROR_STRINGS=1"
        "ZSTD_HAVE_WEAK_SYMBOLS=0" 
        "ZSTD_TRACE=0"
    )
    if(HDIFFPATCH_MT)
        list(APPEND DEP_DEFINITIONS "ZSTD_MULTITHREAD=1")
    endif()
endif()

# MD5
if(HDIFFPATCH_USE_MD5)
    list(APPEND DEP_SOURCES "../libmd5/md5.c")
    list(APPEND DEP_INCLUDES "../libmd5")
    list(APPEND DEP_DEFINITIONS "_ChecksumPlugin_md5")
endif()

# XXH
if(HDIFFPATCH_USE_XXH)
    list(APPEND DEP_INCLUDES "../xxHash")
    list(APPEND DEP_DEFINITIONS "_ChecksumPlugin_xxh3" "_ChecksumPlugin_xxh128")
endif()

# LDEF (LibDeflate)
if(HDIFFPATCH_USE_LDEF)
    set(LDEF_DIR "../libdeflate")
    list(APPEND DEP_INCLUDES ${LDEF_DIR})
    list(APPEND DEP_DEFINITIONS "_CompressPlugin_ldef")
    
    list(APPEND DEP_SOURCES 
        "${LDEF_DIR}/lib/deflate_decompress.c"
        "${LDEF_DIR}/lib/utils.c"
        "${LDEF_DIR}/lib/deflate_compress.c"
    )
    # Check for x86 specific file
    file(GLOB LDEF_X86 "${LDEF_DIR}/lib/x86/cpu_features.c")
    if(LDEF_X86)
         list(APPEND DEP_SOURCES ${LDEF_X86})
    endif()
    
    if(HDIFFPATCH_USE_ZLIB)
        list(APPEND DEP_DEFINITIONS "_CompressPlugin_ldef_is_use_zlib=1")
    else()
        list(APPEND DEP_DEFINITIONS "_CompressPlugin_ldef_is_use_zlib=0")
    endif()
endif()

# 搜集库文件
file(GLOB_RECURSE libHDiffPatch "../libHDiffPatch/*")
if(HDIFFPATCH_MT)
    file(GLOB_RECURSE libParallel "../libParallel/*")
else()
    set(libParallel "")
endif()

# 搜集源文件
file(GLOB_RECURSE SOURCES "source/*.cpp")

# 搜集公开头文件
file(GLOB_RECURSE PUBLIC_HEADERS "include/*.h")

# 构建库
add_library(HDiffPatchStatic STATIC ${libHDiffPatch} ${libParallel} ${SOURCES} ${PUBLIC_HEADERS} ${DEP_SOURCES})
add_library(HDiffPatchShared SHARED ${libHDiffPatch} ${libParallel} ${SOURCES} ${PUBLIC_HEADERS} ${DEP_SOURCES})

# 设置包含目录
foreach(TARGET HDiffPatchStatic HDiffPatchShared)
    target_include_directories(${TARGET} 
        PUBLIC "include"
        PRIVATE "source"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.."
        PRIVATE ${DEP_INCLUDES}
    )
    target_compile_definitions(${TARGET} PRIVATE ${DEP_DEFINITIONS})
endforeach()

# 设置库的输出名称（不带 Static/Shared 后缀）
set_target_properties(HDiffPatchStatic PROPERTIES OUTPUT_NAME "HDiffPatch")
set_target_properties(HDiffPatchShared PROPERTIES OUTPUT_NAME "HDiffPatch")

# 设置库的输出目录，区分静态库和动态库以避免文件名冲突
set_target_properties(HDiffPatchStatic PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/static"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/static"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/static"
)

set_target_properties(HDiffPatchShared PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
)

# 库类型宏
target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_STATIC_LIB=1)
target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_STATIC_LIB=0)


# 为不同平台添加平台宏定义
if(WIN32)
    target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_PLATFORM_WINDOWS)
    target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_PLATFORM_WINDOWS)
elseif(ANDROID)
    target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_PLATFORM_ANDROID)
    target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_PLATFORM_ANDROID)
    #for resolve can't find __android_log_print
    target_link_libraries(HDiffPatchStatic log)
    target_link_libraries(HDiffPatchShared log)
elseif(IOS)
    target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_PLATFORM_IOS)
    target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_PLATFORM_IOS)
    set_target_properties(HDiffPatchStatic PROPERTIES XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
    
    # 配置 iOS Framework
    set_target_properties(HDiffPatchShared PROPERTIES
        FRAMEWORK TRUE
        MACOSX_FRAMEWORK_IDENTIFIER "com.hdiffpatch.HDiffPatch"
        PUBLIC_HEADER "${PUBLIC_HEADERS}"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_PLATFORM_LINUX)
    target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_PLATFORM_LINUX)
    target_link_libraries(HDiffPatchStatic stdc++)
    target_link_libraries(HDiffPatchShared stdc++)
elseif(APPLE)
    target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_PLATFORM_MACOS)
    target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_PLATFORM_MACOS)
endif()

message("print NDK_ROOT=$ENV{NDK_ROOT}")

# 添加导出宏
target_compile_definitions(HDiffPatchStatic PRIVATE HDIFFPATCH_EXPORTS)
target_compile_definitions(HDiffPatchShared PRIVATE HDIFFPATCH_EXPORTS)