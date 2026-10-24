cmake_minimum_required(VERSION 3.15)

project(
    ems_backend
    VERSION 1.0.0
    LANGUAGES C CXX
)

# 自有后端默认使用 C++20；公共契约库可在目标上单独指定 C++17。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(EMS_WITH_ZEROMQ "Build the ZeroMQ notification and command channels" OFF)
option(EMS_ENABLE_ARCH_TUNING "Enable architecture-specific tuning flags" ON)
option(EMS_TARGET_LINUX_ARM "Enable Linux ARM target configuration" OFF)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(EMS_ROOT_PROJECT ON)
endif()

# MSVC 使用原生参数；调试/优化参数由 CMake 按配置提供。
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

# 所有库输出到统一目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" EMS_SYSTEM_PROCESSOR_LOWER)
set(EMS_IS_ARM64 OFF)

if(EMS_SYSTEM_PROCESSOR_LOWER MATCHES "^(aarch64|arm64)$")
    set(EMS_IS_ARM64 ON)
endif()

if(EMS_ENABLE_ARCH_TUNING
   AND EMS_IS_ARM64
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
)
    include(CheckCXXCompilerFlag)

    # Generic ARMv8 baseline for ARM64 Linux builds.
    check_cxx_compiler_flag("-march=armv8-a" EMS_FLAG_MARCH_ARMV8A_SUPPORTED)

    if(EMS_FLAG_MARCH_ARMV8A_SUPPORTED)
        add_compile_options(-march=armv8-a)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 统一查找平台依赖，Windows 可通过 CMAKE_PREFIX_PATH 指定安装前缀。
include(Dependencies)

# 先定义第三方目标，再开启自有代码的警告，避免给第三方追加项目警告。
add_subdirectory(third_party)

if(MSVC)
    add_compile_options(/W4 /we4715)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror=return-type)
endif()

add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)
add_compile_definitions(DEFAULT_ROOT_DIR="${CMAKE_BINARY_DIR}")
include_directories(src)

# 自有库先于测试和示例创建，保证链接关系与测试发现使用实际目标。
if(BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/models)

add_subdirectory(src)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 代码格式化目标（format / format-check），规则见仓库根目录 .clang-format
include(Format)

# 复制 conf/ 目录到构建目录
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/conf" DESTINATION "${CMAKE_BINARY_DIR}")

message(STATUS "========================================")
message(STATUS "EMS Backend Configuration")
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Configurations: ${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "Target system: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Source directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Build directory: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build examples: ${BUILD_EXAMPLES}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "ARM64 detected: ${EMS_IS_ARM64}")
message(STATUS "Arch tuning enabled: ${EMS_ENABLE_ARCH_TUNING}")
message(STATUS "Linux ARM target requested: ${EMS_TARGET_LINUX_ARM}")
message(STATUS "========================================")
