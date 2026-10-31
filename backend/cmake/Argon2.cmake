include_guard(GLOBAL)
include(FetchContent)

# 官方参考实现；固定归档和摘要，离线可使用 FETCHCONTENT_SOURCE_DIR_EMS_ARGON2_SOURCE。
FetchContent_Declare(
    ems_argon2_source
    URL https://codeload.github.com/P-H-C/phc-winner-argon2/tar.gz/refs/tags/20190702
    URL_HASH SHA256=daf972a89577f8772602bf2eb38b6a3dd3d922bf5724d45e7f9589b5e830442c
)
FetchContent_MakeAvailable(ems_argon2_source)
add_library(ems_argon2 STATIC
    ${ems_argon2_source_SOURCE_DIR}/src/argon2.c
    ${ems_argon2_source_SOURCE_DIR}/src/core.c
    ${ems_argon2_source_SOURCE_DIR}/src/encoding.c
    ${ems_argon2_source_SOURCE_DIR}/src/ref.c
    ${ems_argon2_source_SOURCE_DIR}/src/blake2/blake2b.c
)
target_include_directories(ems_argon2 SYSTEM PUBLIC ${ems_argon2_source_SOURCE_DIR}/include)
target_compile_definitions(ems_argon2 PRIVATE ARGON2_NO_THREADS)
set_target_properties(ems_argon2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
