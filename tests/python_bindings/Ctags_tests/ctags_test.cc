#include "test_suite.h"
#include <iostream>

int main() {
  auto &config = Config::get();
  config.project.ctags_command = "ctags";
#ifdef _WIN32
  std::string slash = "\\";
  config.project.cmake.command =
      "cmake -G\"MSYS Makefiles\" -DCMAKE_INSTALL_PREFIX=/mingw64";
#else
  auto slash = "/";
  config.project.cmake.command = "cmake";
#endif
  auto suite_name = "Ctags_tests";

  {
    auto doTest = [&](auto test) {
      auto test_suite = suite(suite_name);
      {
        auto module = py::module::import("ctags_test");
        test_suite.has_assertion = false;
        auto project_path = (test_suite.test_file_path / "cmake_project")
                                .make_preferred()
                                .string();
        try {
          module.attr(test)(project_path, slash);
          test_suite.has_assertion = true;
        }
        catch(const std::exception &error) {
          std::cout << error.what();
        }
      }
    };

    doTest("get_location");
    doTest("get_locations");
  }
}
