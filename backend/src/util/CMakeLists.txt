add_library(ems_excel STATIC excel.cpp)

target_include_directories(ems_excel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(ems_excel PRIVATE OpenXLSX::OpenXLSX)
if(TARGET pugixml::pugixml)
    target_link_libraries(ems_excel PRIVATE pugixml::pugixml)
else()
    target_link_libraries(ems_excel PRIVATE pugixml::static)
endif()
target_compile_features(ems_excel PUBLIC cxx_std_17)
set_target_properties(
    ems_excel
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)
