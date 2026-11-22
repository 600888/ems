# 静态集成固定第三方源码，不引入上游的全局选项、示例和测试。
set(EMS_DLT645_ROOT "${EMS_BACKEND_ROOT}/third_party/dlt645")

add_library(ems_dlt645_asio INTERFACE)
target_include_directories(ems_dlt645_asio SYSTEM INTERFACE "${EMS_DLT645_ROOT}/third/asio/include")
target_compile_definitions(ems_dlt645_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_THREADS)
if(WIN32)
    target_compile_definitions(ems_dlt645_asio INTERFACE _WIN32_WINNT=0x0A00)
    target_link_libraries(ems_dlt645_asio INTERFACE ws2_32 mswsock)
endif()

file(GLOB_RECURSE EMS_DLT645_SOURCES CONFIGURE_DEPENDS "${EMS_DLT645_ROOT}/src/*.cpp")

add_library(ems_dlt645_stack STATIC ${EMS_DLT645_SOURCES})
target_include_directories(
    ems_dlt645_stack PUBLIC "${EMS_DLT645_ROOT}/include" "${EMS_DLT645_ROOT}/third"
)
target_include_directories(ems_dlt645_stack PRIVATE "${EMS_DLT645_ROOT}/src")

# 主工程的 util/log_base.h 与上游同名；隔离继承目录，确保协议栈使用自己的实现。
set_property(
    TARGET ems_dlt645_stack
    PROPERTY INCLUDE_DIRECTORIES
             "${EMS_DLT645_ROOT}/include;${EMS_DLT645_ROOT}/third;${EMS_DLT645_ROOT}/src"
)

target_link_libraries(ems_dlt645_stack PUBLIC Threads::Threads PRIVATE ems_dlt645_asio)

get_directory_property(log_definitions COMPILE_DEFINITIONS)
if(WIN32 AND "SPDLOG_WCHAR_FILENAMES" IN_LIST log_definitions)
    # 只调整构建副本，让上游日志初始化器适配统一的 spdlog 宽字符文件名。
    set(log_header "${EMS_DLT645_ROOT}/include/log/log_init.hpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${log_header}")
    file(READ "${log_header}" log_source)
    string(REPLACE "(directory / filename).string()" "(directory / filename).native()"
                   log_source "${log_source}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/log")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/include/log/log_init.hpp" "${log_source}")
    target_include_directories(ems_dlt645_stack BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
endif()

set_target_properties(
    ems_dlt645_stack
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
)

# PIMPL 隐藏协议栈类型，插件通过 C ABI 调用。
add_library(ems_dlt645_core STATIC dlt645_protocol.cpp)
# 避免上游 singleton.hpp 被主工程同名头替换，定义注册表的类型须保持一致。
target_include_directories(
    ems_dlt645_core BEFORE PRIVATE "${EMS_DLT645_ROOT}/include" "${EMS_DLT645_ROOT}/third"
)

target_include_directories(ems_dlt645_core PUBLIC "${EMS_BACKEND_ROOT}/include")
target_include_directories(
    ems_dlt645_core SYSTEM
    PRIVATE "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include"
)
target_link_libraries(ems_dlt645_core PRIVATE ems_dlt645_stack ems_dlt645_asio)

set_target_properties(
    ems_dlt645_core
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
)

add_library(proto_dlt645 MODULE dlt645_plugin.cpp)
target_include_directories(proto_dlt645 PRIVATE "${EMS_BACKEND_ROOT}/include")
target_compile_definitions(proto_dlt645 PRIVATE EMS_BUILD_PROTOCOL_PLUGIN)
target_link_libraries(proto_dlt645 PRIVATE ems_dlt645_core)

set_target_properties(
    proto_dlt645
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
               LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(WIN32)
    set_target_properties(proto_dlt645 PROPERTIES PREFIX "")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(proto_dlt645 PRIVATE dlt645_plugin.def)
    endif()
elseif(APPLE)
    set_target_properties(proto_dlt645 PROPERTIES SUFFIX ".so")
endif()

# 协议栈静态编入插件。
install(
    TARGETS proto_dlt645
    LIBRARY DESTINATION plugins/dlt645/1.0.0
    RUNTIME DESTINATION plugins/dlt645/1.0.0
)

install(FILES "${EMS_BACKEND_ROOT}/include/net/protocol/dlt645_protocol.h"
              "${EMS_BACKEND_ROOT}/include/net/protocol/protocol_plugin_api.h"
        DESTINATION include/net/protocol
)

file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    CONTENT
        "{\n  \"plugin_id\": \"dlt645\",\n  \"package_version\": \"1.0.0\",\n  \"abi_major\": 1,\n  \"binary\": \"$<TARGET_FILE_NAME:proto_dlt645>\",\n  \"protocols\": [\"DLT645\"],\n  \"roles\": [\"client\", \"server_readonly\"]\n}\n"
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    DESTINATION plugins/dlt645/1.0.0
    RENAME manifest.json
)
