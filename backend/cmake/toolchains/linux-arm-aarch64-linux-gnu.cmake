# Toolchain for cross-compiling EMS backend to Linux ARM64 (aarch64)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Tell project CMake to use the generic Linux ARM target profile.
set(EMS_TARGET_LINUX_ARM ON CACHE BOOL "Enable Linux ARM target configuration" FORCE)

# Prefer target sysroot paths for libraries/includes when cross compiling.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
