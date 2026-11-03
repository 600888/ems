set(EMS_LIBIEC61850_ROOT "${EMS_BACKEND_ROOT}/third_party/libiec61850")
if(NOT EXISTS "${EMS_LIBIEC61850_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "IEC61850 sources missing: ${EMS_LIBIEC61850_ROOT}")
endif()

# normal 变量与 CMP0077 隔离上游通用选项，不改父工程 BUILD_TESTS 缓存。
function(ems_configure_libiec61850)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    # 底层硬上限高于公开允许的 100，实际连接数由每个 IedServerConfig 决定。
    set(CONFIG_MAXIMUM_TCP_CLIENT_CONNECTIONS 128)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_TESTS OFF)
    set(BUILD_PYTHON_BINDINGS OFF)
    set(CONFIG_INCLUDE_GOOSE_SUPPORT OFF)
    set(CONFIG_IEC61850_L2_GOOSE OFF)
    set(CONFIG_IEC61850_L2_SMV OFF)
    set(CONFIG_IEC61850_R_GOOSE OFF)
    set(CONFIG_IEC61850_R_SMV OFF)
    set(CONFIG_USE_EXTERNAL_MBEDTLS_DYNLIB OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # EXCLUDE_FROM_ALL 同时避免把上游共享库及安装规则带入 EMS 包。
    add_subdirectory("${EMS_LIBIEC61850_ROOT}" "stack" EXCLUDE_FROM_ALL)
endfunction()
ems_configure_libiec61850()
target_compile_definitions(hal PRIVATE DEBUG_SOCKET=$<BOOL:${DEBUG_SOCKET}>)
# Windows 下上游静态库与共享库导入库同名，集中输出目录会使 Ninja 产生重复规则。
set_target_properties(iec61850 PROPERTIES OUTPUT_NAME ems_iec61850_stack)
# 入库栈在分段报告分支误置 hasSequenceNumber，导致 hasSubSeqNum 永远为 false。
# 修正构建副本，保持公开 getter 语义；原始第三方文件不变。
set(report_source "${EMS_LIBIEC61850_ROOT}/src/iec61850/client/client_report.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${report_source}")
file(READ "${report_source}" report_code)
string(REPLACE "\r\n" "\n" report_code "${report_code}")
set(segment_flag "matchingReport->moreSegementsFollow = MmsValue_getBoolean(moreSegmentsFollow);\n        }\n\n        matchingReport->hasSequenceNumber = true;")
string(REPLACE "${segment_flag}"
    "matchingReport->moreSegementsFollow = MmsValue_getBoolean(moreSegmentsFollow);\n        }\n\n        matchingReport->hasSubSequenceNumber = true;"
    report_code "${report_code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/client_report.c" "${report_code}")
get_target_property(stack_sources iec61850 SOURCES)
list(REMOVE_ITEM stack_sources "./iec61850/client/client_report.c")
list(APPEND stack_sources "${CMAKE_CURRENT_BINARY_DIR}/client_report.c")
set_property(TARGET iec61850 PROPERTY SOURCES "${stack_sources}")
if(MSVC)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/client_report.c" PROPERTIES LANGUAGE CXX)
endif()
if(WIN32)
    # 只改构建目录中的 HAL 副本，使监听端口独占；第三方源码保持原样。
    set(socket_source "${EMS_LIBIEC61850_ROOT}/hal/socket/win32/socket_win32.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${socket_source}")
    file(READ "${socket_source}" socket_code)
    string(REPLACE "SO_REUSEADDR" "SO_EXCLUSIVEADDRUSE" socket_code "${socket_code}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/socket_win32.c" "${socket_code}")
    get_target_property(hal_sources hal SOURCES)
    list(REMOVE_ITEM hal_sources "${socket_source}")
    list(APPEND hal_sources "${CMAKE_CURRENT_BINARY_DIR}/socket_win32.c")
    set_property(TARGET hal PROPERTY SOURCES "${hal_sources}")
    if(MSVC)
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/socket_win32.c" PROPERTIES LANGUAGE CXX)
    endif()
endif()

add_library(ems_libiec61850 INTERFACE)
target_link_libraries(ems_libiec61850 INTERFACE iec61850 Threads::Threads)
target_include_directories(
    ems_libiec61850 SYSTEM INTERFACE
    "${EMS_LIBIEC61850_ROOT}/src/iec61850/inc"
    "${EMS_LIBIEC61850_ROOT}/src/mms/inc"
    "${EMS_LIBIEC61850_ROOT}/src/common/inc"
    "${EMS_LIBIEC61850_ROOT}/src/logging"
    "${EMS_LIBIEC61850_ROOT}/hal/inc"
)
foreach(target IN ITEMS iec61850 hal)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON
                          C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
endforeach()
if(WIN32)
    target_link_libraries(ems_libiec61850 INTERFACE ws2_32 iphlpapi bcrypt)
endif()

add_library(ems_iec61850_core STATIC iec61850_protocol.cpp mms_server.cpp)
target_include_directories(ems_iec61850_core PUBLIC "${EMS_BACKEND_ROOT}/include")
target_include_directories(ems_iec61850_core SYSTEM PRIVATE
    "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include")
target_link_libraries(ems_iec61850_core PRIVATE ems_libiec61850)

add_library(proto_iec61850 MODULE iec61850_plugin.cpp)
target_include_directories(proto_iec61850 PRIVATE "${EMS_BACKEND_ROOT}/include")
target_compile_definitions(proto_iec61850 PRIVATE EMS_BUILD_PROTOCOL_PLUGIN)
target_link_libraries(proto_iec61850 PRIVATE ems_iec61850_core)
foreach(target IN ITEMS ems_iec61850_core proto_iec61850)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
endforeach()
set_target_properties(proto_iec61850 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(WIN32)
    set_target_properties(proto_iec61850 PROPERTIES PREFIX "")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(proto_iec61850 PRIVATE iec61850_plugin.def)
    endif()
elseif(APPLE)
    set_target_properties(proto_iec61850 PROPERTIES SUFFIX ".so"
        INSTALL_RPATH "@loader_path;@loader_path/deps")
else()
    set_target_properties(proto_iec61850 PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/deps")
endif()

install(TARGETS proto_iec61850 LIBRARY DESTINATION plugins/iec61850/1.0.0
        RUNTIME DESTINATION plugins/iec61850/1.0.0)
install(FILES "${EMS_BACKEND_ROOT}/include/net/protocol/iec61850_protocol.h"
        DESTINATION include/net/protocol)
install(FILES "${EMS_LIBIEC61850_ROOT}/COPYING" DESTINATION plugins/iec61850/1.0.0
        RENAME LICENSE.libiec61850)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json" CONTENT
    "{\n  \"plugin_id\": \"iec61850\",\n  \"package_version\": \"1.0.0\",\n  \"abi_major\": 1,\n  \"binary\": \"$<TARGET_FILE_NAME:proto_iec61850>\",\n  \"protocols\": [\"IEC61850\"],\n  \"roles\": [\"client\", \"server_readonly\"],\n  \"services\": [\"read\", \"dataset_read\", \"reports\", \"server_readonly\"],\n  \"tls\": false\n}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
        DESTINATION plugins/iec61850/1.0.0 RENAME manifest.json)
if(EMS_PROTOCOL_RUNTIME_DEPENDENCIES)
    add_custom_command(TARGET proto_iec61850 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES}
                "$<TARGET_FILE_DIR:proto_iec61850>" COMMAND_EXPAND_LISTS)
    if(WIN32)
        install(FILES ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES} DESTINATION plugins/iec61850/1.0.0)
    else()
        install(FILES ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES} DESTINATION plugins/iec61850/1.0.0/deps)
    endif()
endif()
