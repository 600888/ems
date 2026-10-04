# 原有设备处理器保留给存量调用；动态插件不再依赖其 C++ 接口或 libmodbuspp。
if(TARGET modbuspp-shared)
    add_library(modbus_protocol STATIC modbus_client.cpp modbus_server.cpp)

    target_include_directories(
        modbus_protocol
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${EMS_BACKEND_ROOT}/3rd"
               "${EMS_BACKEND_ROOT}/3rd/libmodbuspp/include" "${EMS_BACKEND_ROOT}/include"
    )

    target_link_libraries(
        modbus_protocol PUBLIC ${LIBMODBUS_LIBRARIES} modbuspp-shared Threads::Threads
    )

    set_target_properties(modbus_protocol PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# 支持包管理器或显式指定同平台/同工具链 libmodbus 安装路径。
if(LIBMODBUS_INCLUDE_DIRS AND LIBMODBUS_LIBRARIES)
    set(EMS_MODBUS_INCLUDE_DIRS "${LIBMODBUS_INCLUDE_DIRS}")
    set(EMS_MODBUS_LIBRARIES "${LIBMODBUS_LIBRARIES}")
else()
    find_path(
        LIBMODBUS_INCLUDE_DIR
        NAMES modbus.h
        PATH_SUFFIXES modbus
    )

    find_library(LIBMODBUS_LIBRARY NAMES modbus libmodbus)

    if(NOT LIBMODBUS_INCLUDE_DIR OR NOT LIBMODBUS_LIBRARY)
        message(
            FATAL_ERROR
                "libmodbus >= 3.1.6 is required; set LIBMODBUS_INCLUDE_DIR and LIBMODBUS_LIBRARY"
        )
    endif()
    set(EMS_MODBUS_INCLUDE_DIRS "${LIBMODBUS_INCLUDE_DIR}")
    set(EMS_MODBUS_LIBRARIES "${LIBMODBUS_LIBRARY}")
endif()

add_library(ems_libmodbus INTERFACE)

target_include_directories(ems_libmodbus SYSTEM INTERFACE ${EMS_MODBUS_INCLUDE_DIRS})

target_link_libraries(ems_libmodbus INTERFACE ${EMS_MODBUS_LIBRARIES})

if(WIN32)
    target_link_libraries(ems_libmodbus INTERFACE ws2_32)
endif()

add_library(ems_modbus_core STATIC modbus_protocol.cpp)

target_include_directories(ems_modbus_core PUBLIC "${EMS_BACKEND_ROOT}/include")

target_include_directories(
    ems_modbus_core SYSTEM
    PRIVATE "${EMS_BACKEND_ROOT}/3rd/libmodbuspp/3rdparty/json/single_include"
)

target_link_libraries(ems_modbus_core PRIVATE ems_libmodbus Threads::Threads)

set_target_properties(
    ems_modbus_core
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
)

add_library(proto_modbus MODULE modbus_plugin.cpp)

target_include_directories(proto_modbus PRIVATE "${EMS_BACKEND_ROOT}/include")

target_compile_definitions(proto_modbus PRIVATE EMS_BUILD_PROTOCOL_PLUGIN)

target_link_libraries(proto_modbus PRIVATE ems_modbus_core)

set_target_properties(
    proto_modbus
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
               LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(WIN32)
    set_target_properties(proto_modbus PROPERTIES PREFIX "")
elseif(APPLE)
    set_target_properties(
        proto_modbus PROPERTIES SUFFIX ".so" INSTALL_RPATH "@loader_path;@loader_path/deps"
    )
else()
    set_target_properties(proto_modbus PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/deps")
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_sources(proto_modbus PRIVATE modbus_plugin.def)
endif()

install(
    TARGETS proto_modbus
    LIBRARY DESTINATION plugins/modbus/2.0.0
    RUNTIME DESTINATION plugins/modbus/2.0.0
)

install(FILES "${EMS_BACKEND_ROOT}/include/net/protocol/modbus_protocol.h"
              "${EMS_BACKEND_ROOT}/include/net/protocol/protocol_plugin_api.h"
        DESTINATION include/net/protocol
)

# 包版本清单及显式运行依赖；Windows 依赖与插件同目录。
file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    CONTENT
        "{\n  \"plugin_id\": \"modbus\",\n  \"package_version\": \"2.0.0\",\n  \"abi_major\": 1,\n  \"binary\": \"$<TARGET_FILE_NAME:proto_modbus>\",\n  \"protocols\": [\"ModbusTCP\", \"ModbusRTU\"],\n  \"roles\": [\"client\", \"server_readonly\"]\n}\n"
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest-$<CONFIG>.json"
    DESTINATION plugins/modbus/2.0.0
    RENAME manifest.json
)
set(EMS_PROTOCOL_RUNTIME_DEPENDENCIES
    ""
    CACHE STRING "Runtime dependency files to deploy with protocol modules"
)

if(EMS_PROTOCOL_RUNTIME_DEPENDENCIES)
    add_custom_command(
        TARGET proto_modbus
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES}
                "$<TARGET_FILE_DIR:proto_modbus>"
        COMMAND_EXPAND_LISTS
    )

    if(WIN32)
        install(FILES ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES} DESTINATION plugins/modbus/2.0.0)
    else()
        install(FILES ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES} DESTINATION plugins/modbus/2.0.0/deps)
    endif()
endif()
