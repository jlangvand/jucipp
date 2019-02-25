#include "python_interpreter.h"

pybind11::module Python::Interpreter::add_module(const std::string &module_name) {
  return pybind11::reinterpret_borrow<pybind11::module>(PyImport_AddModule(module_name.c_str()));
}

pybind11::object Python::Interpreter::error() {
  return pybind11::reinterpret_borrow<pybind11::object>(PyErr_Occurred());
}

pybind11::module Python::Interpreter::reload_module(pybind11::module &module) {
  auto reload = pybind11::reinterpret_steal<pybind11::module>(PyImport_ReloadModule(module.ptr()));
  if(!reload) {
    throw pybind11::error_already_set();
  }
  return reload;
}

#include <pybind11/embed.h>
#include <iostream>

Python::Interpreter::~Interpreter() {
  if (error()){
    std::cout << py::error_already_set().what() << std::endl;
  }
  py::finalize_interpreter();
}

Python::Interpreter::Interpreter() {
  py::initialize_interpreter();
}
