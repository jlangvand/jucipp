#include "tiny_process_module.hpp"
#include <process.hpp>

void TinyProcessModule::init_module(py::module &api) {
  py::class_<TinyProcessLib::Process, std::shared_ptr<TinyProcessLib::Process>> process(api, "Process");
  process
      // .def("kill", (void (TinyProcessLib::Process::*)(TinyProcessLib::Process::id_type, bool)) & TinyProcessLib::Process::kill,
      //      py::arg("id"),
      //      py::arg("force") = false)
      // .def(py::init<const TinyProcessLib::Process::string_type &, const TinyProcessLib::Process::string_type &>(),
      //      py::arg("command"),
      //      py::arg("path") = TinyProcessLib::Process::string_type())
      .def("get_id", &TinyProcessLib::Process::get_id)
      .def("get_exit_status", &TinyProcessLib::Process::get_exit_status)
      .def("try_get_exit_status", &TinyProcessLib::Process::try_get_exit_status,
           py::arg("exit_status"))
      .def("write", (bool (TinyProcessLib::Process::*)(const char *, size_t)) & TinyProcessLib::Process::write,
           py::arg("bytes"),
           py::arg("n"))
      .def("write", (bool (TinyProcessLib::Process::*)(const std::string &)) & TinyProcessLib::Process::write,
           py::arg("string"))
      .def("close_stdin", &TinyProcessLib::Process::close_stdin)
      .def("kill", (void (TinyProcessLib::Process::*)(bool)) & TinyProcessLib::Process::kill,
           py::arg("force"))

      ;
}
