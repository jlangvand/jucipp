#include "cmake.h"
#include "test_suite.h"
#include <iostream>

int main() {
  auto suite_name = "CompileCommands_tests";
  suite test_suite(suite_name);
  auto project_path = (test_suite.test_file_path / "cmake_project");
  auto &config = Config::get();
  config.project.cmake.command = "cmake";
  CMake cmake(project_path);
  cmake.update_default_build(project_path/"build");
  try {
    auto module = py::module::import("compile_commands_test");
    module.attr("run")(project_path.string());
    test_suite.has_assertion = true;
  }
  catch(const py::error_already_set &error) {
    std::cout << error.what();
  }
}
