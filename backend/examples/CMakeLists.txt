# EMS Backend Examples
# This directory contains various example programs demonstrating
# the usage of EMS backend libraries

add_subdirectory(protocol)

add_subdirectory(net)

add_executable(ems_point_import point_table_import.cpp)
target_link_libraries(ems_point_import PRIVATE ems_db)
