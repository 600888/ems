# 实时存储后端的独立测试工程，可在 Windows / Linux 上运行。

cmake_minimum_required(VERSION 3.16)
project(ems_realtime_tests LANGUAGES C CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_subdirectory(../../src/realtime realtime)
set(gtest_force_shared_crt
    ON
    CACHE BOOL "Use shared runtime" FORCE
)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(../../3rd/googletest googletest EXCLUDE_FROM_ALL)

add_executable(test_realtime_backends test_backends.cpp)

target_include_directories(test_realtime_backends PRIVATE ../../src/realtime)

target_link_libraries(test_realtime_backends PRIVATE ems_realtime_core gtest Threads::Threads)

if(EMS_REALTIME_WITH_SHM)
    target_link_libraries(test_realtime_backends PRIVATE ems_realtime_shm)

    target_compile_definitions(ems_realtime_shm PRIVATE EMS_REALTIME_TEST_HOOKS)

    target_compile_definitions(test_realtime_backends PRIVATE EMS_REALTIME_TEST_SHM)
endif()

if(EMS_REALTIME_WITH_REDIS)
    target_link_libraries(test_realtime_backends PRIVATE ems_realtime_redis)

    target_compile_definitions(test_realtime_backends PRIVATE EMS_REALTIME_TEST_REDIS)
endif()

add_test(NAME realtime_backends COMMAND test_realtime_backends)
