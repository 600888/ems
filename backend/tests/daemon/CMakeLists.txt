find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(scenarios normal forced abandoned cleanup_failure)
if(WIN32)
    list(APPEND scenarios window_close)
endif()
foreach(scenario IN LISTS scenarios)
    add_test(
        NAME daemon_cleanup_${scenario}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle_test.py
                $<TARGET_FILE:ems-daemon> $<TARGET_FILE:ems-rtdb>
                $<TARGET_FILE:test_realtime_backends> ${scenario}
    )
    set_tests_properties(daemon_cleanup_${scenario} PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
endforeach()

if(WIN32)
    foreach(scenario killed window_close)
        add_test(
            NAME daemon_parent_exit_${scenario}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/windows_parent_exit_test.py
                    $<TARGET_FILE:ems-daemon> ${scenario}
        )
        set_tests_properties(daemon_parent_exit_${scenario} PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    endforeach()
endif()
