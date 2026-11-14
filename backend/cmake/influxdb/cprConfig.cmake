# Reuse the dependency target built by ems_add_influxdb without installing it first.
if(NOT TARGET cpr::cpr)
    message(FATAL_ERROR "cpr::cpr must be created before configuring influxdb-cxx")
endif()
set(cpr_FOUND TRUE)
