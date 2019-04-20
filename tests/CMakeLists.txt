add_compile_options(-fno-access-control)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-thread-safety)
endif()

add_definitions(-DJUCI_BUILD_PATH="${CMAKE_BINARY_DIR}" -DJUCI_TESTS_PATH="${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/libclangmm/src
  ${CMAKE_SOURCE_DIR}/tiny-process-library
)

add_library(test_stubs OBJECT
  stubs/config.cpp
  stubs/dialogs.cpp
  stubs/directories.cpp
  stubs/info.cpp
  stubs/notebook.cpp
  stubs/project.cpp
  stubs/selection_dialog.cpp
)

<<<<<<< HEAD
if(BUILD_TESTING)
  add_executable(process_test process_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(process_test juci_shared)
  add_test(process_test process_test)
  
  add_executable(compile_commands_test compile_commands_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(compile_commands_test juci_shared)
  add_test(compile_commands_test compile_commands_test)
  
  add_executable(filesystem_test filesystem_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(filesystem_test juci_shared)
  add_test(filesystem_test filesystem_test)
  
  add_executable(cmake_build_test cmake_build_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(cmake_build_test juci_shared)
  add_test(cmake_build_test cmake_build_test)
  
  add_executable(meson_build_test meson_build_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(meson_build_test juci_shared)
  add_test(meson_build_test meson_build_test)
  
  add_executable(source_test source_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(source_test juci_shared)
  add_test(source_test source_test)
  
  add_executable(source_clang_test source_clang_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(source_clang_test juci_shared)
  add_test(source_clang_test source_clang_test)
  
  add_executable(source_generic_test source_generic_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(source_generic_test juci_shared)
  add_test(source_generic_test source_generic_test)
  
  add_executable(source_key_test source_key_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(source_key_test juci_shared)
  add_test(source_key_test source_key_test)
  
  add_executable(terminal_test terminal_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(terminal_test juci_shared)
  add_test(terminal_test terminal_test)
  
  add_executable(usages_clang_test usages_clang_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(usages_clang_test juci_shared)
  add_test(usages_clang_test usages_clang_test)
  
  if(LIBLLDB_FOUND)
    add_executable(lldb_test lldb_test.cpp $<TARGET_OBJECTS:test_stubs>)
    target_link_libraries(lldb_test juci_shared)
    add_test(lldb_test lldb_test)
    add_subdirectory("lldb_test_files")
  endif()
  
  add_executable(git_test git_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(git_test juci_shared)
  add_test(git_test git_test)
  
  add_executable(ctags_grep_test ctags_grep_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(ctags_grep_test juci_shared)
  add_test(ctags_grep_test ctags_grep_test)
  
  add_executable(tooltips_test tooltips_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(tooltips_test juci_shared)
  add_test(tooltips_test tooltips_test)
  
  add_executable(utility_test utility_test.cpp $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(utility_test juci_shared)
  add_test(utility_test utility_test)
endif()

if(BUILD_FUZZING)
  add_executable(cmake_fuzzer fuzzers/cmake.cpp $<TARGET_OBJECTS:test_stubs>)
  target_compile_options(cmake_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_options(cmake_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_libraries(cmake_fuzzer juci_shared)
  
  add_executable(ctags_fuzzer fuzzers/ctags.cpp $<TARGET_OBJECTS:test_stubs>)
  target_compile_options(ctags_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_options(ctags_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_libraries(ctags_fuzzer juci_shared)
  
  add_executable(docstring_fuzzer fuzzers/docstring.cpp $<TARGET_OBJECTS:test_stubs>)
  target_compile_options(docstring_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_options(docstring_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_libraries(docstring_fuzzer juci_shared)
  
  add_executable(doxygen_fuzzer fuzzers/doxygen.cpp $<TARGET_OBJECTS:test_stubs>)
  target_compile_options(doxygen_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_options(doxygen_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_libraries(doxygen_fuzzer juci_shared)
  
  add_executable(grep_fuzzer fuzzers/grep.cpp $<TARGET_OBJECTS:test_stubs>)
  target_compile_options(grep_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_options(grep_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_libraries(grep_fuzzer juci_shared)
  
  add_executable(markdown_fuzzer fuzzers/markdown.cpp $<TARGET_OBJECTS:test_stubs>)
  target_compile_options(markdown_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_options(markdown_fuzzer PRIVATE -fsanitize=address,fuzzer)
  target_link_libraries(markdown_fuzzer juci_shared)
=======
add_executable(process_test process_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(process_test juci_shared ${PYTHON_LIBRARIES})
add_test(process_test process_test)

add_executable(compile_commands_test compile_commands_test.cc $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(compile_commands_test juci_shared ${PYTHON_LIBRARIES})
add_test(compile_commands_test compile_commands_test)

add_executable(filesystem_test filesystem_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(filesystem_test juci_shared ${PYTHON_LIBRARIES})
add_test(filesystem_test filesystem_test)

add_executable(cmake_build_test cmake_build_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(cmake_build_test juci_shared ${PYTHON_LIBRARIES})
add_test(cmake_build_test cmake_build_test)

add_executable(meson_build_test meson_build_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(meson_build_test juci_shared ${PYTHON_LIBRARIES})
add_test(meson_build_test meson_build_test)

add_executable(source_test source_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(source_test juci_shared ${PYTHON_LIBRARIES})
add_test(source_test source_test)

add_executable(source_clang_test source_clang_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(source_clang_test juci_shared ${PYTHON_LIBRARIES})
add_test(source_clang_test source_clang_test)

add_executable(source_generic_test source_generic_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(source_generic_test juci_shared ${PYTHON_LIBRARIES})
add_test(source_generic_test source_generic_test)

add_executable(source_key_test source_key_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(source_key_test juci_shared ${PYTHON_LIBRARIES})
add_test(source_key_test source_key_test)

add_executable(terminal_test terminal_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(terminal_test juci_shared ${PYTHON_LIBRARIES})
add_test(terminal_test terminal_test)

add_executable(usages_clang_test usages_clang_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(usages_clang_test juci_shared ${PYTHON_LIBRARIES})
add_test(usages_clang_test usages_clang_test)

if(LIBLLDB_FOUND)
  add_executable(lldb_test lldb_test.cc $<TARGET_OBJECTS:test_stubs>)
  target_link_libraries(lldb_test juci_shared ${PYTHON_LIBRARIES})
  add_test(lldb_test lldb_test)
  add_subdirectory("lldb_test_files")
>>>>>>> de6858a... fix tests after moving pybind code
endif()

add_executable(git_test git_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(git_test juci_shared ${PYTHON_LIBRARIES})
add_test(git_test git_test)

add_executable(python_interpreter_test python_interpreter_test.cc $<TARGET_OBJECTS:test_stubs>)
target_link_libraries(python_interpreter_test juci_shared ${PYTHON_LIBRARIES})
add_test(python_interpreter_test python_interpreter_test)

