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

# 搜集库文件
file(GLOB_RECURSE libHDiffPatch "../libHDiffPatch/*")
file(GLOB_RECURSE libParallel "../libParallel/*")

# 搜集源文件
file(GLOB_RECURSE SOURCES "source/*.cpp")

# 搜集公开头文件
file(GLOB_RECURSE PUBLIC_HEADERS "include/*.h")

# 构建库
add_library(HDiffPatchStatic STATIC ${libHDiffPatch} ${libParallel} ${SOURCES} ${PUBLIC_HEADERS})
add_library(HDiffPatchShared SHARED ${libHDiffPatch} ${libParallel} ${SOURCES} ${PUBLIC_HEADERS})

# 设置包含目录
foreach(TARGET HDiffPatchStatic HDiffPatchShared)
    target_include_directories(${TARGET} 
        PUBLIC "include"
        PRIVATE "source"
    )
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