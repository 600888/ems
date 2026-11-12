dlt698_add_test(transport
    SOURCES memory_test.cpp serial_link_test.cpp
    LIBS dlt698)

# TCP 用例依赖 Asio 事件循环与真实回环 socket，单独成文件便于独立运行与定位问题。
# 统一库显式启用 Asio 线程支持，避免 MinGW 中因包含顺序导致的类型不一致。
dlt698_add_test(transport_tcp
    SOURCES tcp_test.cpp
    LIBS dlt698)
set_tests_properties(transport_tcp PROPERTIES
    TIMEOUT 120
    LABELS "network")
