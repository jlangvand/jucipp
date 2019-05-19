#include "test_suite.h"
#include <iostream>

int main() {
  auto &config = Config::get();
  auto suite_name = "Config_tests";
  suite test_suite(suite_name);
  try {
    py::module::import("config_test");
    test_suite.has_assertion = true;
  } catch(const py::error_already_set &error){
    std::cout << error.what();
  }
}
