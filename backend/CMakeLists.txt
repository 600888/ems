cmake_minimum_required(VERSION 3.15)
project(ems_backend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)
option(EMS_ENABLE_ARCH_TUNING "Enable architecture-specific tuning flags" ON)
option(EMS_TARGET_LINUX_ARM "Enable Linux ARM target configuration" OFF)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(EMS_ROOT_PROJECT ON)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror=return-type")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# 所有库输出到统一目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" EMS_SYSTEM_PROCESSOR_LOWER)
set(EMS_IS_ARM64 OFF)
if(EMS_SYSTEM_PROCESSOR_LOWER MATCHES "^(aarch64|arm64)$")
    set(EMS_IS_ARM64 ON)
endif()

if(EMS_ENABLE_ARCH_TUNING AND EMS_IS_ARM64)
    include(CheckCXXCompilerFlag)

    # Generic ARMv8 baseline for ARM64 Linux builds.
    check_cxx_compiler_flag("-march=armv8-a" EMS_FLAG_MARCH_ARMV8A_SUPPORTED)
    if(EMS_FLAG_MARCH_ARMV8A_SUPPORTED)
        add_compile_options(-march=armv8-a)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)

# Find nlohmann_json
set(nlohmann_json_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMODBUS REQUIRED libmodbus>=3.1.6)
include_directories(${LIBMODBUS_INCLUDE_DIRS})

include_directories(src)

add_subdirectory(include)
add_subdirectory(tests)
add_subdirectory(3rd)
add_subdirectory(src)
add_subdirectory(examples)

# 复制 conf/ 目录到构建目录
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/conf DESTINATION ${CMAKE_BINARY_DIR})

message(STATUS "========================================")
message(STATUS "EMS Backend Configuration")
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Source directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Build directory: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build examples: ON")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "ARM64 detected: ${EMS_IS_ARM64}")
message(STATUS "Arch tuning enabled: ${EMS_ENABLE_ARCH_TUNING}")
message(STATUS "Linux ARM target requested: ${EMS_TARGET_LINUX_ARM}")
message(STATUS "========================================")
