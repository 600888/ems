# common/db - EMS 数据库操作动态库
# 封装 ormpp 数据库操作，编译为平台动态库（Linux .so / Windows .dll）。
#
# 同时定义公共模块 targets（website/docs/common/design.md 第 4 节编译目标与依赖方向）：
# 公共库自研目标统一 C++17；纯头接口使用 INTERFACE target；普通公共实现
# 首期静态链接；不提供聚合 ems_common target（避免协议插件被动链接全部能力）。

# ---------------------------------------------------------------------------
# ems_common_core：结果/上下文/身份/时间（纯 C++17 标准库，无第三方依赖）
# ---------------------------------------------------------------------------
add_library(ems_common_core INTERFACE)

target_include_directories(ems_common_core INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_compile_features(ems_common_core INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# ems_common_model：共享模型、纯校验与 ModelView（依赖 core）
# ---------------------------------------------------------------------------
add_library(
    ems_common_model STATIC model/validation.cpp model/model_view.cpp model/device_config.cpp
                            model/device_models.cpp
)

target_include_directories(ems_common_model PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(ems_common_model PUBLIC ems_common_core)

set_target_properties(
    ems_common_model
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# ems_messaging_api：事件信封、投递与事件总线接口（依赖 model/core）
# ---------------------------------------------------------------------------
add_library(ems_messaging_api INTERFACE)

target_include_directories(ems_messaging_api INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(ems_messaging_api INTERFACE ems_common_core)

target_compile_features(ems_messaging_api INTERFACE cxx_std_17)

if(EMS_WITH_ZEROMQ)
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(EMS_LIBZMQ QUIET IMPORTED_TARGET libzmq>=4.3)
    endif()

    if(EMS_LIBZMQ_FOUND)
        set(EMS_LIBZMQ_TARGET PkgConfig::EMS_LIBZMQ)
    else()
        find_path(EMS_LIBZMQ_INCLUDE_DIR zmq.h REQUIRED)

        find_library(EMS_LIBZMQ_LIBRARY NAMES zmq libzmq REQUIRED)

        add_library(ems_libzmq UNKNOWN IMPORTED)

        set_target_properties(
            ems_libzmq PROPERTIES IMPORTED_LOCATION "${EMS_LIBZMQ_LIBRARY}"
                                  INTERFACE_INCLUDE_DIRECTORIES "${EMS_LIBZMQ_INCLUDE_DIR}"
        )
        set(EMS_LIBZMQ_TARGET ems_libzmq)
    endif()

    add_library(
        ems_zmq_messaging STATIC messaging/zmq_change_channel.cpp messaging/zmq_command_channel.cpp
    )

    add_library(ems_zmq_changes ALIAS ems_zmq_messaging)

    target_include_directories(ems_zmq_messaging PUBLIC ${CMAKE_SOURCE_DIR}/include)

    target_link_libraries(
        ems_zmq_messaging
        PUBLIC ems_messaging_api
        PRIVATE ${EMS_LIBZMQ_TARGET} Threads::Threads
    )

    set_target_properties(
        ems_zmq_messaging
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
                   POSITION_INDEPENDENT_CODE ON
    )
endif()

# ---------------------------------------------------------------------------
# ems_common_codec：契约编解码与精度检查（JSON 为私有实现，不公开暴露）
# ---------------------------------------------------------------------------
add_library(ems_common_codec STATIC codec/model_codec.cpp codec/event_codec.cpp)

target_include_directories(ems_common_codec PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_include_directories(
    ems_common_codec PRIVATE ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(ems_common_codec PUBLIC ems_common_model ems_messaging_api)

set_target_properties(
    ems_common_codec
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# 接口 targets：config / database / observability（P2-P4 实现）
# ---------------------------------------------------------------------------
add_library(ems_config_api INTERFACE)

target_include_directories(ems_config_api INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(ems_config_api INTERFACE ems_common_model ems_common_codec)

target_compile_features(ems_config_api INTERFACE cxx_std_17)

add_library(ems_database_api INTERFACE)

target_include_directories(ems_database_api INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(ems_database_api INTERFACE ems_common_core)

target_compile_features(ems_database_api INTERFACE cxx_std_17)

add_subdirectory(db)

add_library(ems_observability INTERFACE)

target_include_directories(ems_observability INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(ems_observability INTERFACE ems_common_core)

target_compile_features(ems_observability INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# ems_realtime_api：实时存储抽象接口与后端工厂（依赖 core；公共头不含 hiredis 等）
# ---------------------------------------------------------------------------
add_library(ems_realtime_api INTERFACE)

target_include_directories(ems_realtime_api INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(ems_realtime_api INTERFACE ems_common_core)

target_compile_features(ems_realtime_api INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# ems_realtime_core：RecordKey 公共验证器
# ---------------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../realtime ${CMAKE_CURRENT_BINARY_DIR}/realtime)

# ---------------------------------------------------------------------------
# ems_config_core：配置校验默认实现（依赖 model 纯校验）
# ---------------------------------------------------------------------------
add_library(ems_config_core STATIC config/catalog_validator.cpp config/point_table.cpp)

target_include_directories(ems_config_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_include_directories(ems_config_core PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(ems_config_core PUBLIC ems_config_api PRIVATE ems_excel)

set_target_properties(
    ems_config_core
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

# ===========================================================================
