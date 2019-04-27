#pragma once
#include "python_interpreter.h"
#include <pybind11/embed.h>

class __attribute__((visibility("default")))
Plugins {
public:
  Plugins();
  void load();

private:
  class Module {
  public:
    static PyObject *init_jucipp_module();
  };

  py::detail::embedded_module jucipp_module;
  Python::Interpreter interpreter;
};
