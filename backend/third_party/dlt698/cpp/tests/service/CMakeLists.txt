dlt698_add_test(service
    SOURCES object_test.cpp service_test.cpp standard_object_test.cpp standard_records_test.cpp sync_test.cpp advanced_test.cpp
    LIBS dlt698)

dlt698_add_test(device SOURCES device_test.cpp LIBS dlt698)
