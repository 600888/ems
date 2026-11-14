include_guard(GLOBAL)

option(EMS_FETCH_CPR "Build pinned cpr/curl sources when no cpr package is installed" ON)

function(ems_add_influxdb)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(BUILD_SHARED_LIBS OFF)
    find_package(cpr CONFIG QUIET)
    if(NOT TARGET cpr::cpr)
        if(NOT EMS_FETCH_CPR)
            message(FATAL_ERROR "influxdb-cxx requires cpr; set cpr_DIR/CMAKE_PREFIX_PATH or EMS_FETCH_CPR=ON")
        endif()
        include(FetchContent)
        set(CPR_BUILD_TESTS OFF)
        set(CPR_BUILD_TESTS_SSL OFF)
        set(CPR_CURL_NOSIGNAL ON)
        set(CPR_USE_SYSTEM_LIB_PSL ON)
        set(CURL_USE_LIBPSL OFF)
        set(CURL_USE_LIBSSH2 OFF)
        set(CURL_USE_LIBIDN2 OFF)
        set(CURL_BROTLI OFF)
        set(CURL_ZSTD OFF)
        set(CURL_ZLIB OFF CACHE STRING "Optional curl compression dependency")
        set(BUILD_LIBCURL_DOCS OFF)
        set(BUILD_MISC_DOCS OFF)
        # Override cpr's curl URL with the official archive; retain its pinned checksum.
        fetchcontent_declare(curl
            URL https://curl.se/download/curl-8.13.0.tar.xz
            URL_HASH SHA256=4a093979a3c2d02de2fbc00549a32771007f2e78032c6faa5ecd2f7a9e152025)
        fetchcontent_declare(ems_cpr
            URL https://codeload.github.com/libcpr/cpr/tar.gz/refs/tags/1.12.0
            URL_HASH SHA256=f64b501de66e163d6a278fbb6a95f395ee873b7a66c905dd785eae107266a709)
        fetchcontent_makeavailable(ems_cpr)
        # The vendored client calls find_package even when cpr::cpr already exists.
        set(cpr_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/influxdb")
    endif()

    # execute() returns raw JSON without Boost's lossy query() conversion to double.
    set(INFLUXCXX_WITH_BOOST OFF)
    set(INFLUXCXX_TESTING OFF)
    set(INFLUXCXX_SYSTEMTEST OFF)
    set(INFLUXCXX_COVERAGE OFF)
    add_subdirectory("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/influxdb-cxx"
                     "${CMAKE_CURRENT_BINARY_DIR}/influxdb-cxx")
    foreach(target IN ITEMS InfluxDB InfluxDB-Core InfluxDB-Internal InfluxDB-BoostSupport)
        target_compile_definitions(${target} PUBLIC INFLUXDB_STATIC_DEFINE)
    endforeach()
endfunction()
