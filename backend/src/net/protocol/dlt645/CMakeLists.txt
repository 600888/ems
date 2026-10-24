# 静态集成固定第三方源码，不引入上游的全局选项、示例和测试。
set(EMS_DLT645_ROOT "${EMS_BACKEND_ROOT}/third_party/dlt645")

# 只使用 Boost 头文件。优先安装包；也支持 BOOST_ROOT 指定的源码/头文件目录。
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

target_link_libraries(ems_dlt645_stack PUBLIC Boost::headers Threads::Threads)
target_compile_definitions(ems_dlt645_stack PRIVATE BOOST_ASIO_NO_DEPRECATED)

set_target_properties(
    ems_dlt645_stack
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
)

if(WIN32)
    target_link_libraries(ems_dlt645_stack PUBLIC ws2_32 mswsock)
    target_compile_definitions(ems_dlt645_stack PUBLIC _WIN32_WINNT=0x0601)
endif()

# 公开 PIMPL API 与插件桥各自独立；Boost / 第三方类型不跨 ABI 边界。
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
target_link_libraries(ems_dlt645_core PRIVATE ems_dlt645_stack)

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

# 插件运行时不需要独立 dlt645 DLL 或 Boost 动态库。
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
