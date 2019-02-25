#pragma once

#include "config.h"
#include "menu.h"
#include "python_interpreter.h"
#include "python_module.h"
#include <pybind11/embed.h>
#include <pybind11/functional.h>

class __attribute__((visibility("default")))
Plugins {
public:
  Plugins() : jucipp_module("Jucipp", Module::init_jucipp_module) {
    auto &config = Config::get();
    config.load();
    auto sys = py::module::import("sys");
    sys.attr("path").cast<py::list>().append(config.plugins.path);
    // sys.attr("excepthook") = py::cpp_function([](py::object type, py::object value, py::object traceback) {
    //   Terminal::get().print("const std::string &message\n");
    // });
  }
  void load() {
    boost::filesystem::directory_iterator end_it;
    for(boost::filesystem::directory_iterator it(Config::get().plugins.path); it != end_it; it++) {
      auto module_name = it->path().stem().string();
      if(module_name.empty())
        continue;
      const auto is_directory = boost::filesystem::is_directory(it->path());
      const auto has_py_extension = it->path().extension() == ".py";
      const auto is_pycache = module_name == "__pycache__";
      if((is_directory && !is_pycache) || has_py_extension) {
        try {
          auto module = interpreter.add_module(module_name);
          if (module) {
            Terminal::get().print("Reloading plugin ´" + module_name + "´\n");
            interpreter.reload_module(module);
          } else {
            Terminal::get().print("Loading plugin ´" + module_name + "´\n");
            py::module::import(module_name.c_str());  
          }
          
        }
        catch(py::error_already_set &error) {
          Terminal::get().print("Error loading plugin `" + module_name + "`:\n" + error.what() + "\n");
        }
      }
      if(interpreter.error())
        std::cerr << py::error_already_set().what() << std::endl;
    }
  }

private:
  py::detail::embedded_module jucipp_module;
  Python::Interpreter interpreter;
};
