cmake_minimum_required(VERSION 3.15)
project(ems_models_test VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../inc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(nlohmann_json 3.9.0 REQUIRED)

add_executable(test_models test_models.cpp)

target_link_libraries(test_models PRIVATE nlohmann_json::nlohmann_json)

target_compile_definitions(test_models PRIVATE
    EMS_VERSION=\"${PROJECT_VERSION}\"
)

enable_testing()
add_test(NAME test_models COMMAND test_models)

message(STATUS "========================================")
message(STATUS "EMS Models Test Configuration")
message(STATUS "========================================")
message(STATUS "Source directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary directory: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "========================================")
