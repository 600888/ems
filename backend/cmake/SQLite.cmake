include_guard(GLOBAL)

option(EMS_FETCH_SQLITE "Build pinned SQLite for Web tests when no installation is found" ${WIN32})

function(ems_require_test_sqlite)
    if(TARGET SQLite::SQLite3)
        return()
    endif()
    find_package(SQLite3 QUIET)
    if(TARGET SQLite::SQLite3)
        return()
    endif()
    if(NOT WIN32 OR NOT EMS_FETCH_SQLITE)
        find_package(SQLite3 REQUIRED)
        return()
    endif()

    message(STATUS "SQLite installation missing; building SQLite 3.50.2 for Web tests")
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    fetchcontent_declare(
        ems_sqlite
        URL https://www.sqlite.org/2025/sqlite-amalgamation-3500200.zip
        URL_HASH SHA256=387991de2834b5da2894119ff4173a9ea0779ea55ebcf53d9a40b24d1dc2484e
    )
    fetchcontent_makeavailable(ems_sqlite)
    add_library(ems_test_sqlite STATIC "${ems_sqlite_SOURCE_DIR}/sqlite3.c")
    add_library(SQLite::SQLite3 ALIAS ems_test_sqlite)
    target_include_directories(ems_test_sqlite SYSTEM PUBLIC "${ems_sqlite_SOURCE_DIR}")
    target_compile_definitions(ems_test_sqlite PRIVATE SQLITE_THREADSAFE=1)
endfunction()
