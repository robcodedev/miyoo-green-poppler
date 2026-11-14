# CMake 3.13+ toolchain for the Miyoo Mini / Mini Plus.
# Use when configuring a fresh ARM build directory.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(MIYOO_TOOLCHAIN_ROOT "/opt/miyoomini-toolchain" CACHE PATH "Miyoo cross compiler prefix")
set(MIYOO_SYSROOT "${MIYOO_TOOLCHAIN_ROOT}/arm-linux-gnueabihf/libc" CACHE PATH "Miyoo ARM sysroot")
set(CMAKE_C_COMPILER "${MIYOO_TOOLCHAIN_ROOT}/usr/bin/arm-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${MIYOO_TOOLCHAIN_ROOT}/usr/bin/arm-linux-gnueabihf-g++")
if(NOT CMAKE_FIND_ROOT_PATH)
    set(CMAKE_FIND_ROOT_PATH "${MIYOO_SYSROOT}")
else()
    list(APPEND CMAKE_FIND_ROOT_PATH "${MIYOO_SYSROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Reliable across CMake regenerations; avoids editing generated flags.make.
get_filename_component(_MIYOO_CXX_LAUNCHER
    "${CMAKE_CURRENT_LIST_DIR}/arm-cxx-launcher.sh" ABSOLUTE)
set(CMAKE_CXX_COMPILER_LAUNCHER "${_MIYOO_CXX_LAUNCHER}" CACHE STRING
    "Filter duplicate ARM system include flags for GCC #include_next" FORCE)
