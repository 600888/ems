# 静态封装仓库内协议栈，不引入上游示例、测试、Python 和全局构建选项。
set(EMS_DLT698_ROOT "${EMS_BACKEND_ROOT}/third_party/dlt698")
file(GLOB_RECURSE EMS_DLT698_SOURCES CONFIGURE_DEPENDS "${EMS_DLT698_ROOT}/cpp/src/*.cpp")
list(FILTER EMS_DLT698_SOURCES EXCLUDE REGEX "/app/(client|server)\\.cpp$")
# 上游静态收尾器不能在 Windows loader lock 内 join。只在构建目录替换内部头引用，
# 使用集成层显式收尾接口，第三方源码保持原样。
foreach(kind IN ITEMS client server)
    set(upstream "${EMS_DLT698_ROOT}/cpp/src/app/${kind}.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${upstream}")
    file(READ "${upstream}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    string(REPLACE "\"detail.hpp\"" "\"dlt698_join_queue.h\"" source "${source}")
    # MSVC 19.39 对嵌套 lambda 内同名 self 的捕获诊断有误，改构建副本的外层变量名。
    string(REPLACE "if (const auto self = weak.lock())\n                    self->manager->post"
        "if (const auto owner = weak.lock())\n                    owner->manager->post" source "${source}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${kind}.cpp" "${source}")
    list(APPEND EMS_DLT698_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${kind}.cpp")
endforeach()
add_library(ems_dlt698_stack STATIC ${EMS_DLT698_SOURCES})
target_include_directories(ems_dlt698_stack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
include(GenerateExportHeader)
generate_export_header(ems_dlt698_stack
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/dlt698/export.hpp"
    EXPORT_MACRO_NAME DLT698_API STATIC_DEFINE DLT698_STATIC)
target_include_directories(ems_dlt698_stack PUBLIC
    "${EMS_DLT698_ROOT}/cpp/include" "${CMAKE_CURRENT_BINARY_DIR}/include")
target_include_directories(ems_dlt698_stack SYSTEM PRIVATE "${EMS_DLT698_ROOT}/third/asio/include")
target_compile_definitions(ems_dlt698_stack PUBLIC DLT698_STATIC
    PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_THREADS)
target_link_libraries(ems_dlt698_stack PUBLIC Threads::Threads)
if(WIN32)
    target_compile_definitions(ems_dlt698_stack PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(ems_dlt698_stack PUBLIC ws2_32 mswsock)
endif()
if(MINGW)
    target_compile_options(ems_dlt698_stack PRIVATE -Wa,-mbig-obj)
endif()

add_library(ems_dlt698_core STATIC dlt698_protocol.cpp)
target_include_directories(ems_dlt698_core PUBLIC "${EMS_BACKEND_ROOT}/include")
target_include_directories(ems_dlt698_core SYSTEM PRIVATE
    "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include")
target_link_libraries(ems_dlt698_core PRIVATE ems_dlt698_stack)
if(MINGW)
    target_compile_options(ems_dlt698_core PRIVATE -Wa,-mbig-obj)
endif()

add_library(proto_dlt698 MODULE dlt698_plugin.cpp)
target_include_directories(proto_dlt698 PRIVATE "${EMS_BACKEND_ROOT}/include")
target_compile_definitions(proto_dlt698 PRIVATE EMS_BUILD_PROTOCOL_PLUGIN)
target_link_libraries(proto_dlt698 PRIVATE ems_dlt698_core)
foreach(target IN ITEMS ems_dlt698_stack ems_dlt698_core proto_dlt698)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
endforeach()
set_target_properties(proto_dlt698 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(WIN32)
    set_target_properties(proto_dlt698 PROPERTIES PREFIX "")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(proto_dlt698 PRIVATE dlt698_plugin.def)
    endif()
elseif(APPLE)
    set_target_properties(proto_dlt698 PROPERTIES SUFFIX ".so")
endif()
install(TARGETS proto_dlt698 LIBRARY DESTINATION plugins/dlt698/1.0.0
    RUNTIME DESTINATION plugins/dlt698/1.0.0)
install(FILES "${EMS_BACKEND_ROOT}/include/net/protocol/dlt698_protocol.h"
    "${EMS_BACKEND_ROOT}/include/net/protocol/protocol_plugin_api.h"
    DESTINATION include/net/protocol)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    CONTENT "{\n  \"plugin_id\": \"dlt698\",\n  \"package_version\": \"1.0.0\",\n  \"abi_major\": 1,\n  \"binary\": \"$<TARGET_FILE_NAME:proto_dlt698>\",\n  \"protocols\": [\"DLT698\"],\n  \"roles\": [\"client\", \"server_readonly\"]\n}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    DESTINATION plugins/dlt698/1.0.0 RENAME manifest.json)
