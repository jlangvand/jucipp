#include "plugins.h"
#include "config.h"
#include "python_module.h"
#include "terminal.h"

Plugins::Plugins() : jucipp_module("Jucipp", Module::init_jucipp_module) {
  auto &config = Config::get();
  config.load();
#ifdef PYTHON_HOME_DIR
#ifdef _WIN32
  const std::wstring python_home(PYTHON_HOME_DIR);
  const std::wstring python_path(python_home + L";" + python_home + L"\\lib-dynload;" + python_home + L"\\site-packages" );
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

void Plugins::load() {
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
        auto module = py::module::import(module_name.c_str());
        Terminal::get().print("Loading plugin ´" + module_name + "´\n");
      }
      catch(py::error_already_set &error) {
        Terminal::get().print("Error loading plugin `" + module_name + "`:\n" + error.what() + "\n");
      }
    }
  }
}
