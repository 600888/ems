add_library(ems_models INTERFACE)
target_include_directories(ems_models INTERFACE ${CMAKE_SOURCE_DIR}/src)

if(nlohmann_json_FOUND)
    target_link_libraries(ems_models INTERFACE nlohmann_json::nlohmann_json)
endif()

message(STATUS "EMS models library configured")
