#include "test_suite.h"
#include "python_module.h"
#include <iostream>

suite::suite(const boost::filesystem::path &path) : plugins(config) {
  py::initialize_interpreter();
  if(!Py_IsInitialized()) {
    throw std::runtime_error("Unable to initialize interpreter");
  }
  auto sys = py::module::import("sys");
  if(!sys) {
    throw std::runtime_error("Unable to append sys path");
  }
  auto sys_path = sys.attr("path").cast<py::list>();
  sys_path.append((test_file_path / path).string());
  sys_path.append((test_file_path).string());
  config.terminal.history_size = 100;
}
suite::~suite() {
  if(Py_IsInitialized()) {
    py::finalize_interpreter();
    g_assert_true(has_assertion);
  }
}
