add_library(ems_models INTERFACE)
target_include_directories(ems_models INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(nlohmann_json_FOUND)
    target_link_libraries(ems_models INTERFACE nlohmann_json::nlohmann_json)
endif()

message(STATUS "EMS models library configured")
