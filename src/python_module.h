#pragma once
#include "python_bind.h"
#include "terminal.h"
#include <boost/filesystem.hpp>
#include <pybind11/stl.h>

namespace pybind11 {
  namespace detail {
    template <>
    struct type_caster<boost::filesystem::path> {
    public:
      PYBIND11_TYPE_CASTER(boost::filesystem::path, _("str"));
      bool load(handle src, bool) {
        const std::string path = pybind11::str(src);
        value = path;
        return !PyErr_Occurred();
      }

      static handle cast(boost::filesystem::path src, return_value_policy, handle) {
        return pybind11::str(src.string());
      }
    };
  }
}

class Module {
  static void init_terminal_module(pybind11::module &api) {
    py::class_<Terminal, std::unique_ptr<Terminal, py::nodelete>>(api, "Terminal")
        .def(py::init([]() { return &(Terminal::get()); }))
        .def("process", (int (Terminal::*)(const std::string &, const boost::filesystem::path &, bool)) & Terminal::process,
             py::arg("command"),
             py::arg("path") = "",
             py::arg("use_pipes") = false)
        .def("async_process", (void (Terminal::*)(const std::string &, const boost::filesystem::path &, const std::function<void(int)> &, bool)) & Terminal::async_process,
             py::arg("command"),
             py::arg("path") = "",
             py::arg("callback") = nullptr,
             py::arg("quiet") = false)
        .def("kill_last_async_process", &Terminal::kill_last_async_process,
             py::arg("force") = false)
        .def("kill_async_processes", &Terminal::kill_async_processes,
             py::arg("force") = false)
        .def("print", &Terminal::print,
             py::arg("message"),
             py::arg("bold") = false)
        .def("async_print", (void (Terminal::*)(const std::string &, bool)) & Terminal::async_print,
             py::arg("message"),
             py::arg("bold") = false)
        .def("async_print", (void (Terminal::*)(size_t, const std::string &)) & Terminal::async_print,
             py::arg("line_nr"),
             py::arg("message"))
        .def("configure", &Terminal::configure)
        .def("clear", &Terminal::clear);
  };

public:
  static auto init_jucipp_module() {
    auto api = py::module("Jucipp", "API");
    Module::init_terminal_module(api);
    return api.ptr();
  };
};
