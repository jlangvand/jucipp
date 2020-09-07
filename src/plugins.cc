#include "plugins.h"
#include "config.hpp"
#include "python_module.h"
#include "terminal.hpp"

Plugins::Plugins(Config &config) : jucipp_module("Jucipp", Module::init_jucipp_module) {
#ifdef PYTHON_HOME_DIR
#ifdef _WIN32
  const std::wstring python_home(PYTHON_HOME_DIR);
  const std::wstring python_path(python_home + L";" + python_home + L"\\lib-dynload;" + python_home + L"\\site-packages");
  Py_SetPythonHome(python_home.c_str());
  Py_SetPath(python_path.c_str());
#endif
#endif
  py::initialize_interpreter();
  py::module::import("sys")
      .attr("path")
      .cast<py::list>()
      .append(config.plugins.path);
}

Plugins::~Plugins() {
  py::finalize_interpreter();
}


void Plugins::init_hook() {
  for(auto &module_name : loaded_modules) {
    auto module = py::module(py::handle(PyImport_GetModule(py::str(module_name.c_str()).ptr())), false);
    if(py::hasattr(module, "init_hook")) {
      py::object obj = module.attr("init_hook");
      if(py::isinstance<py::function>(obj)) {
        py::function func(obj);
        try {
          func();
        }
        catch(const py::error_already_set &err) {
          std::cerr << err.what() << std::endl;
        }
      }
    }
  }
}

void Plugins::load() {
  const auto &plugin_path = Config::get().plugins.path;

  boost::system::error_code ec;
  if(!boost::filesystem::exists(plugin_path, ec)) {
    ec.clear();
    boost::filesystem::create_directories(plugin_path, ec);
  }

  if(ec) {
    std::cerr << ec.message() << std::endl;
    return;
  }

  boost::filesystem::directory_iterator end_it;

  for(boost::filesystem::directory_iterator it(plugin_path); it != end_it; it++) {
    auto module_name = it->path().stem().string();
    if(module_name.empty())
      continue;
    const auto is_directory = boost::filesystem::is_directory(it->path());
    const auto has_py_extension = it->path().extension() == ".py";
    const auto is_pycache = module_name == "__pycache__";
    if((is_directory && !is_pycache) || has_py_extension) {
      try {
        auto module = py::module::import(module_name.c_str());
        loaded_modules.push_back(module_name);
      }
      catch(py::error_already_set &error) {
        std::cerr << "Error loading plugin `" << module_name << "`:\n"
                  << error.what() << "\n";
      }
    }
  }
}
