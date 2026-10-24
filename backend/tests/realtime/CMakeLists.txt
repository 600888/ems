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

add_subdirectory(../../third_party/googletest googletest EXCLUDE_FROM_ALL)

add_executable(test_realtime_backends test_backends.cpp ../../src/realtime/store_detail.cpp)

target_include_directories(test_realtime_backends PRIVATE ../../src/realtime)

target_link_libraries(test_realtime_backends PRIVATE ems_realtime_core gtest Threads::Threads)

if(MSVC)
    target_compile_options(test_realtime_backends PRIVATE /utf-8)
endif()
if(WIN32)
    target_link_libraries(test_realtime_backends PRIVATE bcrypt)
    add_custom_command(TARGET test_realtime_backends POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:test_realtime_backends>
                $<TARGET_FILE_DIR:test_realtime_backends>
        COMMAND_EXPAND_LISTS)
endif()

if(EMS_REALTIME_WITH_SHM)
    target_link_libraries(test_realtime_backends PRIVATE ems_realtime_shm)

    target_compile_definitions(test_realtime_backends PRIVATE EMS_REALTIME_TEST_SHM)

    cmake_policy(PUSH)
    if(POLICY CMP0144)
        cmake_policy(SET CMP0144 NEW)
    endif()
    find_package(Boost 1.83 QUIET CONFIG)
    if(NOT Boost_FOUND)
        if(POLICY CMP0167)
            cmake_policy(SET CMP0167 OLD)
        endif()
        find_package(Boost 1.83 REQUIRED MODULE)
    endif()
    cmake_policy(POP)
    target_link_libraries(test_realtime_backends PRIVATE Boost::headers)
endif()

if(EMS_REALTIME_WITH_REDIS)
    target_link_libraries(test_realtime_backends PRIVATE ems_realtime_redis)

    target_compile_definitions(test_realtime_backends PRIVATE EMS_REALTIME_TEST_REDIS)
endif()

add_test(NAME realtime_backends COMMAND test_realtime_backends)
if(WIN32)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-temp")
    set_tests_properties(realtime_backends PROPERTIES ENVIRONMENT
        "TEMP=${CMAKE_CURRENT_BINARY_DIR}/test-temp;TMP=${CMAKE_CURRENT_BINARY_DIR}/test-temp")
endif()
