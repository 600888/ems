cmake_minimum_required(VERSION 3.15)
project(ems_backend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(EMS_ROOT_PROJECT ON)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror=return-type")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.9.0 QUIET)

if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.10.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(json)
    set(nlohmann_json_FOUND TRUE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMODBUS REQUIRED libmodbus>=3.1.6)
include_directories(${LIBMODBUS_INCLUDE_DIRS})

include_directories(src)

add_subdirectory(inc)
add_subdirectory(tests)
add_subdirectory(3rd)
add_subdirectory(src)
add_subdirectory(examples)

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
message(STATUS "========================================")
