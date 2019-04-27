#include "python_module.h"
#include "cmake.h"
#include "compile_commands.h"
#include "config.h"
#include "ctags.h"
#ifdef JUCI_ENABLE_DEBUG
#include "debug_lldb.h"
#endif
#include "dialogs.h"
#include "terminal.h"
#include "git.h"

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
