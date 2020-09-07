#include "python_module.h"
#include "cmake.hpp"
#include "compile_commands.hpp"
#include "config.hpp"
#include "ctags.hpp"
#ifdef JUCI_ENABLE_DEBUG
#include "debug_lldb.hpp"
#endif
#include "dialogs.hpp"
#include "git.hpp"
#include "terminal.hpp"

PyObject *Module::init_jucipp_module() {
  auto api = py::module("Jucipp", "API");
  CMake::init_module(api);
  CompileCommands::init_module(api);
  Config::init_module(api);
  Ctags::init_module(api);
#ifdef JUCI_ENABLE_DEBUG
  Debug::LLDB::init_module(api);
#endif
  Dialog::init_module(api);
  Dispatcher::init_module(api);
  Git::init_module(api);
  Terminal::init_module(api);
  return api.ptr();
}
