# UE Linux cross-toolchain for HDiffPatch.
# 用 Epic 随引擎发布的 clang 交叉工具链 + libc++ 编库，使 clang 版本 / libc++ ABI
# (std::__1) / glibc 符号版本三者都与 UE 一致；否则 .a 里会残留 libstdc++ 的
# std::thread::_State 等符号，UE 用 libc++ 链接时找不到 -> undefined symbol。
#
# 需要:  env  UE_LINUX_TOOLCHAIN_ROOT -> 解压后的工具链根目录 (含 <arch>/ 子目录)
#        cache UE_LINUX_ARCH          -> 目标 triple
#               x86_64-unknown-linux-gnu | aarch64-unknown-linux-gnu

set(CMAKE_SYSTEM_NAME Linux)

if(NOT UE_LINUX_ARCH)
    set(UE_LINUX_ARCH "x86_64-unknown-linux-gnu")
endif()

if(UE_LINUX_ARCH STREQUAL "aarch64-unknown-linux-gnu")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()

set(_tc "$ENV{UE_LINUX_TOOLCHAIN_ROOT}")
if(NOT _tc)
    message(FATAL_ERROR "UE_LINUX_TOOLCHAIN_ROOT env not set")
endif()

# 兼容两种 bundle 布局: 共享 clang(<root>/bin) 或 per-arch clang(<root>/<arch>/bin)
if(EXISTS "${_tc}/bin/clang++")
    set(_bindir "${_tc}/bin")
elseif(EXISTS "${_tc}/${UE_LINUX_ARCH}/bin/clang++")
    set(_bindir "${_tc}/${UE_LINUX_ARCH}/bin")
else()
    message(FATAL_ERROR "clang++ not found under ${_tc}")
endif()

set(CMAKE_C_COMPILER   "${_bindir}/clang")
set(CMAKE_CXX_COMPILER "${_bindir}/clang++")
set(CMAKE_SYSROOT      "${_tc}/${UE_LINUX_ARCH}")
set(CMAKE_C_COMPILER_TARGET   "${UE_LINUX_ARCH}")
set(CMAKE_CXX_COMPILER_TARGET "${UE_LINUX_ARCH}")

# 与 UE 对齐: libc++ (不是 libstdc++) + lld
set(CMAKE_CXX_FLAGS_INIT           "-stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-stdlib=libc++ -fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
