#include "test_suite.h"
#include <iostream>

int main() {
  auto &config = Config::get();
  config.project.ctags_command = "ctags";
  auto suite_name = "Debug_lldb_tests";
  {
    auto doTest = [&](auto test) {
      auto test_suite = suite(suite_name);
      auto build_path = test_suite.build_file_path / "tests" / "lldb_test_files" / "lldb_test_executable";
      {
        auto module = py::module::import("debug_lldb_test");
        test_suite.has_assertion = false;
        try {
          module.attr(test)(build_path.c_str());
          test_suite.has_assertion = true;
        }
        catch(const std::exception &error) {
          std::cout << error.what();
        }
      }
    };

    doTest("start_on_exit");
  }
}
