
add_library(test_suite test_suite.cc)
target_link_libraries(test_suite juci_shared ${PYTHON_LIBRARIES})

include_directories(${CMAKE_SOURCE_DIR}/tests/python_bindings)

add_executable(pb_python_interpreter_test PythonInterpreter_tests/interpreter_test.cc)
target_link_libraries(pb_python_interpreter_test juci_shared test_suite $<TARGET_OBJECTS:test_stubs>)
add_test(pb_python_interpreter_test pb_python_interpreter_test)

add_executable(pb_terminal_test Terminal_tests/terminal_test.cc)
target_link_libraries(pb_terminal_test juci_shared test_suite $<TARGET_OBJECTS:test_stubs>)
add_test(pb_terminal_test pb_terminal_test)
