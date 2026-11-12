cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" DLT698_VERSION LIMIT_COUNT 1)
    project(dlt698 VERSION ${DLT698_VERSION} LANGUAGES CXX)
    include(CTest)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
find_package(Threads REQUIRED)
option(BUILD_SHARED_LIBS "Build the dlt698 shared library" ON)
option(DLT698_BUILD_TRANSPORT "Build standalone Asio transport" ON)
option(DLT698_BUILD_EXAMPLES "Build examples" ON)
option(DLT698_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

function(dlt698_target_setup target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    get_target_property(dlt698_target_type ${target} TYPE)
    if(dlt698_target_type STREQUAL "SHARED_LIBRARY")
        # 共享库使用相对自身的运行时搜索路径，支持安装目录迁移。
        # 使用相对库自身的路径，使安装前缀移动后仍可加载，避免依赖构建目录或环境变量。
        if(APPLE)
            set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path")
        elseif(UNIX)
            set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
        endif()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        # 公开头文件包含 UTF-8 中文注释，安装后的调用方也须使用相同源文件编码。
        target_compile_options(${target} PUBLIC /utf-8)
        if(DLT698_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        # Data::Payload 是 35 路 std::variant，Debug 的 -O0 会让 libstdc++ 为每个
        # 下标 × 每个 base class 生成独立 COMDAT 段，单个测试单元的段数会撞上
        # GNU as 的 COFF 上限（报 too many sections / file too big）。这里只对
        # Debug 追加 -O1，Release 的 -O3 与 MinSizeRel 的 -Os 不受影响。
        if(CMAKE_CONFIGURATION_TYPES)
            target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-O1>)
        elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${target} PRIVATE -O1)
        endif()
        if(DLT698_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(dlt698
    src/standard/catalog.cpp
    src/standard/records.cpp
    src/standard/capabilities.cpp
    src/common/md5.cpp
    src/security/backend.cpp
    src/common/bytes.cpp
    src/common/executor.cpp
    src/transport/memory.cpp
    src/transport/serial_link.cpp
    src/codec/data_codec.cpp
    src/codec/record_codec.cpp
    src/protocol/link/frame.cpp
    src/protocol/link/fragment.cpp
    src/protocol/apdu/get.cpp
    src/protocol/apdu/get_block.cpp
    src/protocol/apdu/time_tag.cpp
    src/protocol/apdu/mutation.cpp
    src/protocol/apdu/connection.cpp
    src/protocol/apdu/security.cpp
    src/protocol/apdu/exchange.cpp
    src/protocol/apdu/advanced.cpp
    src/protocol/apdu/apdu.cpp
    src/session/session.cpp
    src/service/advanced.cpp
    src/service/object.cpp
    src/service/device.cpp
    src/service/standard_object.cpp
    src/service/memory_records.cpp
    src/service/point_probe.cpp
    src/service/service.cpp
    src/service/sync.cpp)
add_library(dlt698::dlt698 ALIAS dlt698)
set_target_properties(dlt698 PROPERTIES EXPORT_NAME dlt698 VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR} CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
# 所有层共享同一个导出宏；Windows 下所有实现均由 dlt698_EXPORTS 控制 dllexport。
generate_export_header(dlt698 EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/export.hpp"
    EXPORT_MACRO_NAME DLT698_API STATIC_DEFINE DLT698_STATIC)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(dlt698 PUBLIC DLT698_STATIC)
endif()
target_include_directories(dlt698 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
dlt698_target_setup(dlt698)
target_link_libraries(dlt698 PRIVATE Threads::Threads)
# 保留旧组件目标的拼写，别名不生成或安装独立库。
foreach(component core session service)
    add_library(dlt698::${component} ALIAS dlt698)
endforeach()
if(DLT698_BUILD_TRANSPORT)
    add_subdirectory(transport)
    add_subdirectory(app)
    foreach(component transport app)
        add_library(dlt698::${component} ALIAS dlt698)
    endforeach()
    install(DIRECTORY include/dlt698/app DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
    install(FILES include/dlt698/app.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
endif()
if(DLT698_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
install(TARGETS dlt698 EXPORT dlt698Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/dlt698 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "transport" EXCLUDE PATTERN "app" EXCLUDE PATTERN "app.hpp" EXCLUDE)
install(FILES include/dlt698/transport/channel.hpp include/dlt698/transport/memory.hpp
    include/dlt698/transport/serial_link.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698/transport)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/export.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
set(dlt698_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/dlt698")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dlt698Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/dlt698Config.cmake" INSTALL_DESTINATION "${dlt698_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/dlt698ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT dlt698Targets NAMESPACE dlt698:: DESTINATION "${dlt698_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dlt698Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/dlt698ConfigVersion.cmake" DESTINATION "${dlt698_package_dir}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/dlt698")
