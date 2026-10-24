# Source directories
add_subdirectory(common)

add_subdirectory(util)

add_subdirectory(rtdb)

add_subdirectory(net)

add_subdirectory(scd)

if(EMS_BUILD_WEB)
    add_subdirectory(web)
endif()
