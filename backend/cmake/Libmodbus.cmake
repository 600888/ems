include_guard(GLOBAL)

# 上游使用 Autotools / VS 工程；仅在集成层生成 Windows 配置，不修改其源码。
function(ems_add_libmodbus)
    if(TARGET ems_libmodbus_vendor)
        return()
    endif()
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    fetchcontent_declare(
        ems_libmodbus
        URL https://codeload.github.com/stephane/libmodbus/tar.gz/refs/tags/v3.1.10
        URL_HASH SHA256=e93503749cd89fda4c8cf1ee6371a3a9cc1f0a921c165afbbc4fd96d4813fa1a
    )
    fetchcontent_makeavailable(ems_libmodbus)

    set(LIBMODBUS_VERSION_MAJOR 3)
    set(LIBMODBUS_VERSION_MINOR 1)
    set(LIBMODBUS_VERSION_MICRO 10)
    set(LIBMODBUS_VERSION 3.1.10)
    file(MAKE_DIRECTORY "${ems_libmodbus_BINARY_DIR}/include")
    configure_file(
        "${ems_libmodbus_SOURCE_DIR}/src/win32/config.h.win32"
        "${ems_libmodbus_BINARY_DIR}/include/config.h" @ONLY
    )
    configure_file(
        "${ems_libmodbus_SOURCE_DIR}/src/modbus-version.h.in"
        "${ems_libmodbus_BINARY_DIR}/include/modbus-version.h" @ONLY
    )

    add_library(
        ems_libmodbus_vendor SHARED
        "${ems_libmodbus_SOURCE_DIR}/src/modbus.c" "${ems_libmodbus_SOURCE_DIR}/src/modbus-data.c"
        "${ems_libmodbus_SOURCE_DIR}/src/modbus-rtu.c"
        "${ems_libmodbus_SOURCE_DIR}/src/modbus-tcp.c"
    )
    target_include_directories(
        ems_libmodbus_vendor SYSTEM PUBLIC "${ems_libmodbus_SOURCE_DIR}/src"
                                           "${ems_libmodbus_BINARY_DIR}/include"
    )
    target_compile_definitions(ems_libmodbus_vendor PRIVATE DLLBUILD _WIN32_WINNT=0x0600)
    target_link_libraries(ems_libmodbus_vendor PRIVATE ws2_32)
    set_target_properties(
        ems_libmodbus_vendor
        PROPERTIES OUTPUT_NAME modbus
                   PREFIX ""
                   WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    set(EMS_MODBUS_INCLUDE_DIRS
        "${ems_libmodbus_SOURCE_DIR}/src;${ems_libmodbus_BINARY_DIR}/include"
        PARENT_SCOPE
    )
    set(EMS_MODBUS_LIBRARIES
        ems_libmodbus_vendor
        PARENT_SCOPE
    )
    set(EMS_MODBUS_LICENSE_FILE
        "${ems_libmodbus_SOURCE_DIR}/COPYING.LESSER"
        PARENT_SCOPE
    )
endfunction()
