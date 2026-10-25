# 实时存储后端的独立测试工程，可在 Windows / Linux 上运行。

cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ems_rtdb_tests LANGUAGES C CXX)
    enable_testing()
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OutputDirectories.cmake")
    ems_set_output_directories("${CMAKE_BINARY_DIR}")
endif()

find_package(Threads REQUIRED)

if(NOT TARGET ems_rtdb)
    add_subdirectory(../../src/rtdb rtdb)
endif()
ems_set_output_directories("${CMAKE_BINARY_DIR}/tests")
set(gtest_force_shared_crt
    ON
    CACHE BOOL "Use shared runtime" FORCE
)
set(BUILD_SHARED_LIBS OFF)

if(NOT TARGET gtest)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(INSTALL_GTEST OFF)
    add_subdirectory(../../third_party/googletest googletest EXCLUDE_FROM_ALL)
    foreach(target IN ITEMS gtest gtest_main gmock gmock_main)
        ems_set_target_output_directories(${target} "${CMAKE_BINARY_DIR}/tests")
    endforeach()
endif()

add_executable(
    test_realtime_backends test_backends.cpp test_business.cpp point_import_test.cpp
                           ../../src/rtdb/store/store_detail.cpp
)
set_target_properties(test_realtime_backends PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

target_include_directories(test_realtime_backends PRIVATE ../../src/rtdb/store)

target_link_libraries(test_realtime_backends PRIVATE ems_realtime_core gtest Threads::Threads)

if(MSVC)
    target_compile_options(test_realtime_backends PRIVATE /utf-8)
endif()
if(WIN32)
    target_link_libraries(test_realtime_backends PRIVATE bcrypt)
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
if(EMS_REALTIME_WITH_SHM)
    add_executable(
        test_rtdb_dispatcher test_dispatcher.cpp ../../src/net/collect/data_dispatcher.cpp
    )
    target_link_libraries(test_rtdb_dispatcher PRIVATE ems::rtdb gtest_main Threads::Threads)
    target_include_directories(test_rtdb_dispatcher PRIVATE ../../third_party)
    target_compile_definitions(test_rtdb_dispatcher PRIVATE EMS_REALTIME_TEST_SHM)
    target_compile_definitions(test_rtdb_dispatcher PRIVATE FMT_HEADER_ONLY)
    set_target_properties(test_rtdb_dispatcher PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
    if(TARGET redis++)
        target_link_libraries(test_rtdb_dispatcher PRIVATE redis++)
        target_compile_definitions(test_rtdb_dispatcher PRIVATE EMS_NOTIFICATION_WITH_REDIS)
    endif()
    add_test(NAME rtdb_dispatcher COMMAND test_rtdb_dispatcher)
endif()
if(WIN32)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-temp")
    set_tests_properties(
        realtime_backends
        PROPERTIES
            ENVIRONMENT
            "TEMP=${CMAKE_CURRENT_BINARY_DIR}/test-temp;TMP=${CMAKE_CURRENT_BINARY_DIR}/test-temp"
    )
    if(TARGET test_rtdb_dispatcher)
        set_tests_properties(
            rtdb_dispatcher
            PROPERTIES
                ENVIRONMENT
                "TEMP=${CMAKE_CURRENT_BINARY_DIR}/test-temp;TMP=${CMAKE_CURRENT_BINARY_DIR}/test-temp"
        )
    endif()
endif()

ems_copy_directory_runtime_dependencies()
