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

# 自动定位 clang++ (优先目标 arch 子目录，clang 是 cross driver，选哪个 arch 的都行)
file(GLOB_RECURSE _cxx_cands "${_tc}/${UE_LINUX_ARCH}/*clang++")
if(NOT _cxx_cands)
    file(GLOB_RECURSE _cxx_cands "${_tc}/*clang++")
endif()
list(FILTER _cxx_cands EXCLUDE REGEX "\\.(cfg|sh|txt)$")
if(NOT _cxx_cands)
    message(FATAL_ERROR "clang++ not found under ${_tc} (检查 UE_LINUX_TOOLCHAIN_URL 是否正确解压)")
endif()
list(GET _cxx_cands 0 _cxx)
get_filename_component(_bindir "${_cxx}" DIRECTORY)

set(CMAKE_C_COMPILER   "${_bindir}/clang")
set(CMAKE_CXX_COMPILER "${_bindir}/clang++")

# sysroot 必须匹配目标 arch；native bundle 仅含 host(x86_64)，缺该 arch 时明确报错
if(NOT EXISTS "${_tc}/${UE_LINUX_ARCH}")
    message(FATAL_ERROR
        "sysroot ${_tc}/${UE_LINUX_ARCH} 不存在；该 bundle 不含 ${UE_LINUX_ARCH} 架构 "
        "(native-linux bundle 仅含 x86_64，arm64 需用 multiarch 来源)。")
endif()
set(CMAKE_SYSROOT "${_tc}/${UE_LINUX_ARCH}")
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
