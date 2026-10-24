# ============================================================================
# 代码格式化目标（clang-format，规则见仓库根目录 .clang-format）
#
# 提供两个目标：
#   make format        就地格式化 backend 下的自有代码
#   make format-check  仅检查是否符合规则，不符合则以非 0 退出（CI / 提交前用）
#
# 关闭方式：
#   cmake -DEMS_ENABLE_FORMAT_TARGET=OFF ..
#
# 说明：
#   - 只处理 include / src / tests / examples 四个目录，backend/third_party 永不参与。
#   - 显式指定 CLANG_FORMAT_EXECUTABLE 可覆盖自动探测结果，例如交叉编译或
#     多版本共存时：
#       cmake -DCLANG_FORMAT_EXECUTABLE=/usr/bin/clang-format-21 ..
# ============================================================================

option(EMS_ENABLE_FORMAT_TARGET "Enable clang-format targets (format / format-check)" ON)

if(NOT EMS_ENABLE_FORMAT_TARGET)
    return()
endif()

# ---------------------------------------------------------------------------
# 定位 clang-format
# ---------------------------------------------------------------------------
if(NOT CLANG_FORMAT_EXECUTABLE)
    find_program(CLANG_FORMAT_EXECUTABLE
        NAMES
            clang-format
            clang-format-21 clang-format-20 clang-format-19 clang-format-18
            clang-format-17 clang-format-16 clang-format-15 clang-format-14
        DOC "clang-format executable used by the format / format-check targets"
    )
endif()

if(NOT CLANG_FORMAT_EXECUTABLE)
    message(STATUS "clang-format not found - 'format' / 'format-check' targets disabled")
    message(STATUS "  install: python -m pip install clang-format==22.1.8")
    return()
endif()

# 项目固定的 clang-format 版本（见 website/docs/reference/code-format.md）。
# 对齐细节跨版本存在差异，非固定版本可能让 format-check 产生假告警。
set(EMS_CLANG_FORMAT_VERSION "22.1.8")

# 低版本不支持根目录 .clang-format-ignore，会误改 vendored 单头库，直接拒绝
# 注意：--version 写 stdout，必须用两个独立变量，否则 err 会覆盖 out
execute_process(
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --version
    OUTPUT_VARIABLE _cf_version_stdout
    ERROR_VARIABLE _cf_version_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(_cf_version_output "${_cf_version_stdout}${_cf_version_stderr}")
if(_cf_version_output MATCHES "([0-9]+)\\.[0-9]+\\.[0-9]+")
    set(_cf_version "${CMAKE_MATCH_0}")
    set(_cf_major "${CMAKE_MATCH_1}")
    if(_cf_major LESS 18)
        message(WARNING
            "clang-format ${_cf_version} is too old; require >= 18. "
            "The 'format' / 'format-check' targets will NOT be created. "
            "Install with: python -m pip install clang-format==${EMS_CLANG_FORMAT_VERSION}")
        return()
    endif()
    if(NOT _cf_version VERSION_EQUAL EMS_CLANG_FORMAT_VERSION)
        message(WARNING
            "clang-format ${_cf_version} differs from the pinned version "
            "${EMS_CLANG_FORMAT_VERSION}; 'format-check' may report false violations. "
            "Install with: python -m pip install clang-format==${EMS_CLANG_FORMAT_VERSION}")
    endif()
endif()

message(STATUS "clang-format: ${CLANG_FORMAT_EXECUTABLE} (${_cf_version_output})")

# ---------------------------------------------------------------------------
# 收集待格式化文件（排除 third_party）
# ---------------------------------------------------------------------------
set(_ems_format_dirs include src tests examples)
set(_ems_format_files "")

foreach(_dir IN LISTS _ems_format_dirs)
    set(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
    if(NOT IS_DIRECTORY "${_abs}")
        continue()
    endif()
    file(GLOB_RECURSE _globbed
        "${_abs}/*.h" "${_abs}/*.hpp" "${_abs}/*.c" "${_abs}/*.cc" "${_abs}/*.cpp"
    )
    list(APPEND _ems_format_files ${_globbed})
endforeach()

if(NOT _ems_format_files)
    message(STATUS "No C/C++ sources found - 'format' / 'format-check' targets disabled")
    return()
endif()

list(LENGTH _ems_format_files _ems_format_count)
message(STATUS "clang-format will process ${_ems_format_count} files (backend/third_party excluded)")

# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------
add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i --style=file ${_ems_format_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Formatting ${_ems_format_count} C/C++ files with clang-format"
    VERBATIM
    USES_TERMINAL
)

add_custom_target(format-check
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --style=file --dry-run --Werror
            ${_ems_format_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking formatting of ${_ems_format_count} C/C++ files"
    VERBATIM
    USES_TERMINAL
)
