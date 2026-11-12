dlt698_add_test(app SOURCES server_test.cpp client_test.cpp traffic_test.cpp LIBS dlt698)
set_tests_properties(app PROPERTIES TIMEOUT 60 LABELS "network")
