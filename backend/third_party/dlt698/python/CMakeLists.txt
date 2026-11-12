cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" DLT698_VERSION LIMIT_COUNT 1)
project(dlt698_python VERSION ${DLT698_VERSION} LANGUAGES CXX)
find_package(Python 3.11 COMPONENTS Interpreter Development.Module REQUIRED)
if(Python_VERSION VERSION_GREATER_EQUAL "3.15")
    message(FATAL_ERROR "Python 3.11 through 3.14 standard GIL CPython is required")
endif()
execute_process(COMMAND "${Python_EXECUTABLE}" -c
    "import sys,sysconfig; assert sys.implementation.name == 'cpython' and not sysconfig.get_config_var('Py_GIL_DISABLED')"
    RESULT_VARIABLE python_supported)
if(NOT python_supported EQUAL 0)
    message(FATAL_ERROR "Unsupported Python implementation or free-threaded ABI")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static core inside the extension" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "C++ regression runs separately" FORCE)
set(DLT698_BUILD_TRANSPORT ON CACHE BOOL "Managed application transport" FORCE)
set(DLT698_BUILD_EXAMPLES OFF CACHE BOOL "No C++ examples in wheels" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../cpp" cpp EXCLUDE_FROM_ALL)
set(PYBIND11_FINDPYTHON ON)
set(PYBIND11_INSTALL OFF CACHE BOOL "Do not install third-party SDK" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third/pybind11" pybind11 EXCLUDE_FROM_ALL)
execute_process(COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/provenance.py"
    --root "${CMAKE_CURRENT_SOURCE_DIR}/.." --output "${CMAKE_CURRENT_BINARY_DIR}"
    RESULT_VARIABLE provenance_result)
if(NOT provenance_result EQUAL 0)
    message(FATAL_ERROR "Source provenance verification failed")
endif()
pybind11_add_module(_native NO_EXTRAS
    bindings/module.cpp bindings/common/errors.cpp bindings/model/model.cpp
    bindings/model/records.cpp bindings/protocol/apdu/messages.cpp bindings/protocol/apdu/connection.cpp
    bindings/protocol/apdu/mutation.cpp bindings/protocol/apdu/advanced.cpp bindings/app/options.cpp bindings/standard/standard.cpp
    bindings/codec/codec.cpp bindings/app/app.cpp bindings/app/async_server.cpp
    bindings/service/providers.cpp bindings/security/backend.cpp bindings/session/engine.cpp bindings/transport/trans.cpp
    bindings/protocol/fragments.cpp bindings/transport/channels.cpp)
target_compile_features(_native PRIVATE cxx_std_17)
target_include_directories(_native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/session"
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/transport")
target_link_libraries(_native PRIVATE dlt698::dlt698)
# sdist 文件时间可能固定且早于缓存对象；不能只依赖 mtime 判断源码是否变化。
# 将完整输入摘要放入每个项目目标的私有编译参数，摘要变化使核心及绑定一起重新编译。
# 此配置仅存在于 Python 独立构建树，不进入 C++ 的默认构建或安装接口。
file(READ "${CMAKE_CURRENT_BINARY_DIR}/_build_info.json" dlt698_python_identity)
string(JSON dlt698_python_source_sha GET "${dlt698_python_identity}" source_sha256)
foreach(dlt698_python_target dlt698 _native)
    target_compile_definitions(${dlt698_python_target} PRIVATE
        "DLT698_PY_SOURCE_SHA256=\"${dlt698_python_source_sha}\"")
endforeach()
set_target_properties(_native PROPERTIES CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden)
if(MSVC)
    target_compile_options(_native PRIVATE /utf-8 /W4 /bigobj)
endif()
install(TARGETS _native
    LIBRARY DESTINATION dlt698 COMPONENT python
    RUNTIME DESTINATION dlt698 COMPONENT python)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_build_info.json" DESTINATION dlt698 COMPONENT python)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
    DESTINATION dlt698/licenses COMPONENT python RENAME DLT698_LICENSE)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../third/pybind11/LICENSE"
    DESTINATION dlt698/licenses COMPONENT python RENAME PYBIND11_LICENSE)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../third/asio/LICENSE_1_0.txt"
    DESTINATION dlt698/licenses COMPONENT python RENAME ASIO_LICENSE)
