# 数据模型纯头接口，供数据库、实时库和采集模块共享。

add_library(ems_models INTERFACE)

target_include_directories(ems_models INTERFACE ${CMAKE_SOURCE_DIR}/src)

if(nlohmann_json_FOUND)
    target_link_libraries(ems_models INTERFACE nlohmann_json::nlohmann_json)
endif()

message(STATUS "EMS models library configured")
