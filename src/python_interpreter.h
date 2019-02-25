#pragma once
#include "python_bind.h"

namespace Python {
  class Interpreter {
  public:
    pybind11::module static add_module(const std::string &module_name);
    pybind11::module static reload_module(pybind11::module &module);
    pybind11::object static error();
    ~Interpreter();
    Interpreter();
  };
}; // namespace Python
