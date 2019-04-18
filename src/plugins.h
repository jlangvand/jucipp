#pragma once
#include "python_interpreter.h"
#include <boost/filesystem.hpp>
#include <pybind11/embed.h>

namespace pybind11 {
  namespace detail {
    template <>
    struct type_caster<boost::filesystem::path> {
    public:
      PYBIND11_TYPE_CASTER(boost::filesystem::path, _("str"));
      bool load(handle src, bool) {
        value = std::string(pybind11::str(src));
        return !PyErr_Occurred();
      }

      static handle cast(boost::filesystem::path src, return_value_policy, handle) {
        return pybind11::str(src.string());
      }
    };
  } // namespace detail
} // namespace pybind11

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
