#include "test_suite.h"
#include <iostream>

int main() {
  auto &config = Config::get();
#ifdef _WIN32
  config.project.cmake.command = "cmake -G\"MSYS Makefiles\" -DCMAKE_INSTALL_PREFIX=/mingw64";
#else
  config.project.cmake.command = "cmake";
#endif
  auto suite_name = "CMake_tests";
  suite test_suite(suite_name);
  auto module = py::module::import("cmake_test");

  try {
    module.attr("run")((test_suite.test_file_path / "cmake_project").string());
    test_suite.has_assertion = true;
  } catch(const py::error_already_set &error){
    std::cout << error.what();
  }
}
