# common/db - EMS 数据库操作动态库
# 封装 ormpp 数据库操作，编译为 libems_db.so
#
# 同时定义公共模块 targets（docs/common/design.md 第 4 节编译目标与依赖方向）：
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
add_library(ems_common_model STATIC
    model/validation.cpp
    model/model_view.cpp
)
target_include_directories(ems_common_model PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ems_common_model PUBLIC ems_common_core)
set_target_properties(ems_common_model PROPERTIES
    CXX_STANDARD 17
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

# ---------------------------------------------------------------------------
# ems_common_codec：契约编解码与精度检查（JSON 为私有实现，不公开暴露）
# ---------------------------------------------------------------------------
add_library(ems_common_codec STATIC
    codec/model_codec.cpp
    codec/event_codec.cpp
)
target_include_directories(ems_common_codec PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(ems_common_codec PRIVATE
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)
target_link_libraries(ems_common_codec PUBLIC ems_common_model ems_messaging_api)
set_target_properties(ems_common_codec PROPERTIES
    CXX_STANDARD 17
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

add_library(ems_observability INTERFACE)
target_include_directories(ems_observability INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ems_observability INTERFACE ems_common_core)
target_compile_features(ems_observability INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# ems_realtime_api：实时存储抽象接口与后端工厂（依赖 core；公共头不含 hiredis 等）
# ---------------------------------------------------------------------------
add_library(ems_realtime_api INTERFACE)
target_include_directories(ems_realtime_api INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ems_realtime_api INTERFACE ems_common_core)
target_compile_features(ems_realtime_api INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# ems_realtime_core：RecordKey 公共验证器
# ---------------------------------------------------------------------------
add_library(ems_realtime_core STATIC
    realtime/realtime_store.cpp
)
target_include_directories(ems_realtime_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ems_realtime_core PUBLIC ems_realtime_api)
set_target_properties(ems_realtime_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# ems_config_core：配置校验默认实现（依赖 model 纯校验）
# ---------------------------------------------------------------------------
add_library(ems_config_core STATIC
    config/catalog_validator.cpp
)
target_include_directories(ems_config_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ems_config_core PUBLIC ems_config_api)
set_target_properties(ems_config_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

# ===========================================================================

add_library(ems_db SHARED
    db/db_manager.cpp
)

target_include_directories(ems_db
PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd
    ${CMAKE_SOURCE_DIR}/3rd/ormpp/ormpp
    ${CMAKE_SOURCE_DIR}/3rd/ormpp
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(ems_db
PRIVATE
    Threads::Threads
)

# 检测 MySQL 开发库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MYSQL mysqlclient)
endif()
if(NOT MYSQL_FOUND)
    find_package(MySQL COMPONENTS MySQL QUIET)
endif()

if(MYSQL_FOUND)
    target_link_libraries(ems_db PRIVATE ${MYSQL_LIBRARIES})
    target_include_directories(ems_db PRIVATE ${MYSQL_INCLUDE_DIRS})
    target_compile_definitions(ems_db PRIVATE EMS_DB_HAS_MYSQL ORMPP_ENABLE_MYSQL)
    message(STATUS "ems_db: MySQL support enabled")
else()
    message(STATUS "ems_db: MySQL support disabled (libmysqlclient not found)")
endif()

# Optional SQLite3
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 sqlite3)
    if(SQLITE3_FOUND)
        target_link_libraries(ems_db PRIVATE ${SQLITE3_LIBRARIES})
        target_include_directories(ems_db PRIVATE ${SQLITE3_INCLUDE_DIRS})
        message(STATUS "ems_db: SQLite support enabled")
    endif()
endif()

target_compile_definitions(ems_db PRIVATE EMS_DB_BUILD)

set_target_properties(ems_db PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

message(STATUS "EMS DB library configured (shared: libems_db.so)")
