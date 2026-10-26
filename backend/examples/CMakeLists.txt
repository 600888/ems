# EMS Backend Examples
# This directory contains various example programs demonstrating
# the usage of EMS backend libraries

ems_set_output_directories("${CMAKE_BINARY_DIR}/examples")

add_subdirectory(protocol)

add_subdirectory(net)

foreach(example IN ITEMS rtdb_usage rtdb_ems_usage rtdb_manager_usage multi_process_sharing)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE ems::rtdb)
endforeach()

ems_copy_directory_runtime_dependencies()
