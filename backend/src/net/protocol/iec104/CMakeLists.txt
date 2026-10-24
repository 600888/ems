# 编译仓库内的 lib60870 C 栈，避免引入 c104/Python 及第三方全局 CMake 选项。
set(EMS_LIB60870_ROOT "${EMS_BACKEND_ROOT}/third_party/iec104/depends/lib60870/lib60870-C")
set(EMS_LIB60870_SOURCES
    file-service/file_server.c
    iec60870/apl/cpXXtime2a.c
    iec60870/cs101/cs101_asdu.c
    iec60870/cs101/cs101_bcr.c
    iec60870/cs101/cs101_information_objects.c
    iec60870/cs101/cs101_master_connection.c
    iec60870/cs101/cs101_master.c
    iec60870/cs101/cs101_queue.c
    iec60870/cs101/cs101_slave.c
    iec60870/cs104/cs104_connection.c
    iec60870/cs104/cs104_frame.c
    iec60870/cs104/cs104_slave.c
    iec60870/link_layer/buffer_frame.c
    iec60870/link_layer/link_layer.c
    iec60870/link_layer/serial_transceiver_ft_1_2.c
    iec60870/frame.c
    iec60870/lib60870_common.c
    common/linked_list.c
    hal/memory/lib_memory.c
)

if(WIN32)
    list(APPEND EMS_LIB60870_SOURCES hal/serial/win32/serial_port_win32.c
         hal/thread/win32/thread_win32.c hal/time/win32/time.c
    )
elseif(APPLE)
    list(APPEND EMS_LIB60870_SOURCES hal/serial/linux/serial_port_linux.c
         hal/socket/bsd/socket_bsd.c hal/thread/macos/thread_macos.c hal/time/unix/time.c
    )
else()
    list(APPEND EMS_LIB60870_SOURCES hal/serial/linux/serial_port_linux.c
         hal/socket/linux/socket_linux.c hal/thread/linux/thread_linux.c hal/time/unix/time.c
    )
endif()
list(TRANSFORM EMS_LIB60870_SOURCES PREPEND "${EMS_LIB60870_ROOT}/src/")

if(WIN32)
    list(APPEND EMS_LIB60870_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/lib60870_socket_win32.c")
endif()

add_library(ems_lib60870 STATIC ${EMS_LIB60870_SOURCES})

target_include_directories(
    ems_lib60870 SYSTEM
    PUBLIC "${EMS_LIB60870_ROOT}/src/inc/api" "${EMS_LIB60870_ROOT}/src/hal/inc"
           "${EMS_LIB60870_ROOT}/src/common/inc"
)

target_include_directories(
    ems_lib60870 PRIVATE "${EMS_LIB60870_ROOT}/config" "${EMS_LIB60870_ROOT}/src/inc/internal"
                         "${EMS_LIB60870_ROOT}/src/file-service" "${EMS_LIB60870_ROOT}/src"
)

target_compile_definitions(
    ems_lib60870 PRIVATE CONFIG_CS104_SUPPORT_TLS=0 LIB60870_HAS_TLS_SUPPORT=0
)

target_link_libraries(ems_lib60870 PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(ems_lib60870 PUBLIC ws2_32 iphlpapi bcrypt)

    if(MSVC)
        target_compile_definitions(ems_lib60870 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
else()
    target_link_libraries(ems_lib60870 PUBLIC m)
endif()

set_target_properties(
    ems_lib60870 PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
)

add_library(ems_iec104_core STATIC iec104_protocol.cpp)

target_include_directories(ems_iec104_core PUBLIC "${EMS_BACKEND_ROOT}/include")

target_include_directories(
    ems_iec104_core SYSTEM
    PRIVATE "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include"
)

target_link_libraries(ems_iec104_core PRIVATE ems_lib60870 Threads::Threads)

set_target_properties(
    ems_iec104_core
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
)

add_library(proto_iec104 MODULE iec104_plugin.cpp)

target_include_directories(proto_iec104 PRIVATE "${EMS_BACKEND_ROOT}/include")

target_compile_definitions(proto_iec104 PRIVATE EMS_BUILD_PROTOCOL_PLUGIN)

target_link_libraries(proto_iec104 PRIVATE ems_iec104_core)

set_target_properties(
    proto_iec104
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
               LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(WIN32)
    set_target_properties(proto_iec104 PROPERTIES PREFIX "")

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(proto_iec104 PRIVATE iec104_plugin.def)
    endif()
elseif(APPLE)
    set_target_properties(
        proto_iec104 PROPERTIES SUFFIX ".so" INSTALL_RPATH "@loader_path;@loader_path/deps"
    )
else()
    set_target_properties(proto_iec104 PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/deps")
endif()

install(
    TARGETS proto_iec104
    LIBRARY DESTINATION plugins/iec104/1.0.0
    RUNTIME DESTINATION plugins/iec104/1.0.0
)

install(FILES "${EMS_BACKEND_ROOT}/include/net/protocol/iec104_protocol.h"
        DESTINATION include/net/protocol
)

install(
    FILES "${EMS_LIB60870_ROOT}/../COPYING"
    DESTINATION plugins/iec104/1.0.0
    RENAME LICENSE.lib60870
)

file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    CONTENT
        "{\n  \"plugin_id\": \"iec104\",\n  \"package_version\": \"1.0.0\",\n  \"abi_major\": 1,\n  \"binary\": \"$<TARGET_FILE_NAME:proto_iec104>\",\n  \"protocols\": [\"IEC104\"],\n  \"roles\": [\"client\", \"server_readonly\"],\n  \"tls\": false\n}\n"
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    DESTINATION plugins/iec104/1.0.0
    RENAME manifest.json
)

if(EMS_PROTOCOL_RUNTIME_DEPENDENCIES)
    add_custom_command(
        TARGET proto_iec104
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES}
                "$<TARGET_FILE_DIR:proto_iec104>"
        COMMAND_EXPAND_LISTS
    )

    if(WIN32)
        install(FILES ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES} DESTINATION plugins/iec104/1.0.0)
    else()
        install(FILES ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES} DESTINATION plugins/iec104/1.0.0/deps)
    endif()
endif()
