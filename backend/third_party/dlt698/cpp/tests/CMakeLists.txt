# Catch2 以 amalgamated 发行形式引入，只编译一次实现单元；main 位于 catch/catch_main.cpp。
# 测试按被测层次分子目录，每个目录生成一个可执行文件，内部用 TEST_CASE 划分用例。
# 实现单元使用 OBJECT 库，让各测试直接复用实现及 main，无需 whole-archive 链接选项。
add_library(dlt698_catch2 OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third/Catch2/catch_amalgamated.cpp"
    catch/catch_main.cpp)
target_include_directories(dlt698_catch2 SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third/Catch2")
# 暴露 cpp/tests 自身，使各分目录测试可以用 catch/ 前缀包含公用辅助头文件。
target_include_directories(dlt698_catch2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(dlt698_catch2 PUBLIC cxx_std_17)
set_target_properties(dlt698_catch2 PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
# Catch2 自身编译单元不参与本项目的 /W4 与 -Werror，避免第三方告警阻断构建。
if(MSVC)
    target_compile_options(dlt698_catch2 PRIVATE /bigobj /utf-8)
endif()

set(DLT698_TEST_VECTOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/vectors")
# 在函数外先固定绝对路径：CMAKE_CURRENT_SOURCE_DIR 在函数内指向调用方子目录。
set(DLT698_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(DLT698_CATCH2_DIR "${DLT698_TEST_ROOT}/../../third/Catch2")

# 声明一个分目录的测试可执行文件，须在对应的测试子目录中调用。
# name: CTest 中的测试名，同时决定 dlt698_test_<name> 目标名。
# SOURCES: 相对当前测试子目录的源文件列表。
# LIBS: 额外链接的库目标，缺省链接 dlt698。
function(dlt698_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "dlt698_add_test(${name}) 未指定 SOURCES")
    endif()
    if(NOT ARG_LIBS)
        set(ARG_LIBS dlt698)
    endif()
    set(dlt698_test_target dlt698_test_${name})
    set(dlt698_test_sources "")
    foreach(dlt698_test_source IN LISTS ARG_SOURCES)
        # 源文件必须真实存在，否则 add_executable 会静默丢弃并报"无源文件"。
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dlt698_test_source}")
            message(FATAL_ERROR "测试源文件不存在: ${CMAKE_CURRENT_SOURCE_DIR}/${dlt698_test_source}")
        endif()
        list(APPEND dlt698_test_sources "${CMAKE_CURRENT_SOURCE_DIR}/${dlt698_test_source}")
    endforeach()
    add_executable(${dlt698_test_target} ${dlt698_test_sources})
    target_link_libraries(${dlt698_test_target} PRIVATE ${ARG_LIBS})
    # Catch2 实现单元直接编入测试可执行文件，不经过库归档。
    # MinGW下把Catch2 归档后再 whole-archive 链接进来，会与Asio 的静态初始化
    # 顺序冲突，表现为执行期段错误且崩溃点漂移；直接编译可规避该问题。
    target_sources(${dlt698_test_target} PRIVATE
        $<TARGET_OBJECTS:dlt698_catch2>)
    target_include_directories(${dlt698_test_target} SYSTEM PRIVATE "${DLT698_CATCH2_DIR}")
    target_include_directories(${dlt698_test_target} PRIVATE "${DLT698_TEST_ROOT}")
    # DLT698_VECTOR_DIR 供 catch/test_support.hpp 的 fixture() 读取协议向量。
    target_compile_definitions(${dlt698_test_target} PRIVATE
        DLT698_VECTOR_DIR="${DLT698_TEST_VECTOR_DIR}")
    dlt698_target_setup(${dlt698_test_target})
    add_test(NAME ${name} COMMAND ${dlt698_test_target})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

add_subdirectory(common)
add_subdirectory(codec)
add_subdirectory(protocol)
add_subdirectory(service)
add_subdirectory(session)
if(DLT698_BUILD_TRANSPORT)
    add_subdirectory(transport)
    add_subdirectory(app)
endif()

configure_file(package_test.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/package_test.cmake" @ONLY)
add_test(NAME installed_consumer COMMAND ${CMAKE_COMMAND}
    -DCONFIG=$<CONFIG> -P "${CMAKE_CURRENT_BINARY_DIR}/package_test.cmake")
set_tests_properties(installed_consumer PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

add_test(NAME release_manifest COMMAND ${CMAKE_COMMAND}
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-manifest-test"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/tests/release-manifest-test.cmake")
set_tests_properties(release_manifest PROPERTIES TIMEOUT 30)
