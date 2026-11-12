set(DLT698_ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third/asio/include" CACHE PATH "Standalone Asio include directory")
if(NOT EXISTS "${DLT698_ASIO_INCLUDE_DIR}/asio.hpp")
    message(FATAL_ERROR "Standalone Asio not found. Set DLT698_ASIO_INCLUDE_DIR or disable DLT698_BUILD_TRANSPORT.")
endif()
target_sources(dlt698 PRIVATE ../src/transport/tcp.cpp ../src/transport/serial.cpp)
target_include_directories(dlt698 SYSTEM PRIVATE "${DLT698_ASIO_INCLUDE_DIR}")
# 运行时允许跨线程投递，所有翻译单元必须使用同一套有线程 Asio 类型。
# MinGW 的 _MT 可由标准库头文件间接定义；依赖 include 顺序会混用 TLS 键和普通指针。
target_compile_definitions(dlt698 PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_THREADS)
if(WIN32)
    target_compile_definitions(dlt698 PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(dlt698 PRIVATE ws2_32 mswsock)
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/dlt698/transport"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dlt698)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../third/asio/LICENSE_1_0.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dlt698 RENAME ASIO_LICENSE_1_0.txt)
