#include "plugins.h"
#include "python_module.h"

Plugins::Plugins() : jucipp_module("Jucipp", Module::init_jucipp_module) {
#ifdef PYTHON_HOME_DIR
#ifdef _WIN32
  const std::wstring python_home(PYTHON_HOME_DIR);
  const std::wstring python_path(python_home + L";" + python_home + L"\\lib-dynload;" + python_home + L"\\site-packages" );
  Py_SetPythonHome(python_home.c_str());
  Py_SetPath(python_path.c_str());
#endif
#endif
}

void Plugins::load() {}

Plugins::~Plugins() {}
