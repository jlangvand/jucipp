
add_library(test_suite test_suite.cc)
target_link_libraries(test_suite juci_shared ${PYTHON_LIBRARIES})

include_directories(${CMAKE_SOURCE_DIR}/tests/python_bindings)

add_executable(pb_python_module_test PythonModule_tests/python_module_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(pb_python_module_test test_suite)
add_test(pb_python_module_test pb_python_module_test)

add_executable(pb_cmake_test CMake_tests/cmake_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(pb_cmake_test test_suite)
add_test(pb_cmake_test pb_cmake_test)

add_executable(pb_compile_commands_test CompileCommands_tests/compile_commands_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(pb_compile_commands_test test_suite)
add_test(pb_compile_commands_test pb_compile_commands_test)

add_executable(pb_terminal_test Terminal_tests/terminal_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(pb_terminal_test test_suite)
add_test(pb_terminal_test pb_terminal_test)
