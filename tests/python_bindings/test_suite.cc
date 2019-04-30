#include "test_suite.h"
#include "python_module.h"
#include <iostream>

suite::suite(const boost::filesystem::path &path) {
  py::initialize_interpreter();
  auto sys = py::module::import("sys");
  sys.attr("path").cast<py::list>().append((test_file_path / path).string());
  config.terminal.history_size = 100;
}
suite::~suite() {
  py::finalize_interpreter();
  g_assert_true(has_assertion);
}
