#include "debug_lldb.hpp"
#include <cstdio>
#ifdef __APPLE__
#include <cstdlib>
#endif
#include "config.hpp"
#include "filesystem.hpp"
#include "process.hpp"
#include "terminal.hpp"
#include "utility.hpp"
#include <boost/filesystem.hpp>
#include <iostream>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

extern char **environ;

bool Debug::LLDB::initialized = false;

void log(const char *msg, void *) {
  std::cout << "debugger log: " << msg << std::endl;
}

Debug::LLDB::LLDB() : state(lldb::StateType::eStateInvalid), buffer_size(131072) {
  if(!getenv("LLDB_DEBUGSERVER_PATH")) {
#ifndef __APPLE__
    auto debug_server_path = filesystem::get_executable("lldb-server").string();
    if(debug_server_path != "lldb-server") {
#ifdef _WIN32
      Glib::setenv("LLDB_DEBUGSERVER_PATH", debug_server_path.c_str(), 0);
#else
      setenv("LLDB_DEBUGSERVER_PATH", debug_server_path.c_str(), 0);
#endif
    }
#endif
  }
}

void Debug::LLDB::destroy_() {
  {
    LockGuard lock(mutex);
    if(process)
      process->Kill();
  }

  if(debug_thread.joinable())
    debug_thread.join();

  LockGuard lock(mutex);
  if(debugger)
    lldb::SBDebugger::Destroy(*debugger);
}

std::tuple<std::vector<std::string>, std::string, std::vector<std::string>> Debug::LLDB::parse_run_arguments(const std::string &command) {
  std::vector<std::string> environment;
  std::string executable;
  std::vector<std::string> arguments;

  size_t start_pos = std::string::npos;
  bool quote = false;
  bool double_quote = false;
  size_t backslash_count = 0;
  for(size_t c = 0; c <= command.size(); c++) {
    if(c == command.size() || (!quote && !double_quote && backslash_count % 2 == 0 && command[c] == ' ')) {
      if(c > 0 && start_pos != std::string::npos) {
        auto argument = command.substr(start_pos, c - start_pos);
        if(executable.empty()) {
          //Check for environment variable
          bool env_arg = false;
          for(size_t c = 0; c < argument.size(); ++c) {
            if((argument[c] >= 'a' && argument[c] <= 'z') || (argument[c] >= 'A' && argument[c] <= 'Z') ||
               (argument[c] >= '0' && argument[c] <= '9') || argument[c] == '_')
              continue;
            else if(argument[c] == '=' && c + 1 < argument.size()) {
              environment.emplace_back(argument.substr(0, c + 1) + filesystem::unescape_argument(argument.substr(c + 1)));
              env_arg = true;
              break;
            }
            else
              break;
          }

          if(!env_arg)
            executable = filesystem::unescape_argument(argument);
        }
        else
          arguments.emplace_back(filesystem::unescape_argument(argument));
        start_pos = std::string::npos;
      }
    }
    else if(command[c] == '\'' && backslash_count % 2 == 0 && !double_quote)
      quote = !quote;
    else if(command[c] == '"' && backslash_count % 2 == 0 && !quote)
      double_quote = !double_quote;
    else if(command[c] == '\\' && !quote && !double_quote)
      ++backslash_count;
    else
      backslash_count = 0;
    if(c < command.size() && start_pos == std::string::npos && command[c] != ' ')
      start_pos = c;
  }

  return std::make_tuple(environment, executable, arguments);
}

void Debug::LLDB::start(const std::string &command, const boost::filesystem::path &path,
                        const std::vector<std::pair<boost::filesystem::path, int>> &breakpoints,
                        const std::vector<std::string> &startup_commands, const std::string &remote_host) {
  LockGuard lock(mutex);

  if(!debugger) {
    initialized = true;
    lldb::SBDebugger::Initialize();
    debugger = std::make_unique<lldb::SBDebugger>(lldb::SBDebugger::Create(true, log, nullptr));
    listener = std::make_unique<lldb::SBListener>("juCi++ lldb listener");
  }

  //Create executable string and argument array
  auto parsed_run_arguments = parse_run_arguments(command);
  auto &environment_from_arguments = std::get<0>(parsed_run_arguments);
  auto &executable = std::get<1>(parsed_run_arguments);
#ifdef _WIN32
  if(remote_host.empty())
    executable += ".exe";
#endif
  auto &arguments = std::get<2>(parsed_run_arguments);

  std::vector<const char *> argv;
  argv.reserve(arguments.size());
  for(auto &argument : arguments)
    argv.emplace_back(argument.c_str());
  argv.emplace_back(nullptr);

  auto target = debugger->CreateTarget(executable.c_str());
  if(!target.IsValid()) {
    Terminal::get().async_print("Error (debug): Could not create debug target to: " + executable + '\n', true);
    for(auto &handler : on_exit)
      handler(-1);
    return;
  }

  //Set breakpoints
  for(auto &breakpoint : breakpoints) {
    if(!(target.BreakpointCreateByLocation(breakpoint.first.string().c_str(), breakpoint.second)).IsValid()) {
      Terminal::get().async_print("Error (debug): Could not create breakpoint at: " + breakpoint.first.string() + ":" + std::to_string(breakpoint.second) + '\n', true);
      for(auto &handler : on_exit)
        handler(-1);
      return;
    }
  }

  lldb::SBError error;
  if(!remote_host.empty()) {
    auto connect_string = "connect://" + remote_host;
    process = std::make_unique<lldb::SBProcess>(target.ConnectRemote(*listener, connect_string.c_str(), "gdb-remote", error));
    if(error.Fail()) {
      Terminal::get().async_print(std::string("Error (debug): ") + error.GetCString() + '\n', true);
      for(auto &handler : on_exit)
        handler(-1);
      return;
    }
    lldb::SBEvent event;
    while(true) {
      if(listener->GetNextEvent(event)) {
        if((event.GetType() & lldb::SBProcess::eBroadcastBitStateChanged) > 0) {
          auto state = process->GetStateFromEvent(event);
          this->state = state;
          if(state == lldb::StateType::eStateConnected)
            break;
        }
      }
    }

    // Create environment array
    std::vector<const char *> environment;
    environment.reserve(environment_from_arguments.size());
    for(auto &e : environment_from_arguments)
      environment.emplace_back(e.c_str());
    environment.emplace_back(nullptr);

    process->RemoteLaunch(argv.data(), environment.data(), nullptr, nullptr, nullptr, nullptr, lldb::eLaunchFlagNone, false, error);
    if(!error.Fail())
      process->Continue();
  }
  else {
    // Create environment array
    std::vector<const char *> environment;
    environment.reserve(environment_from_arguments.size());
    for(auto &e : environment_from_arguments)
      environment.emplace_back(e.c_str());
    size_t environ_size = 0;
    while(environ[environ_size])
      ++environ_size;
    for(size_t c = 0; c < environ_size; ++c)
      environment.emplace_back(environ[c]);
    environment.emplace_back(nullptr);

    process = std::make_unique<lldb::SBProcess>(target.Launch(*listener, argv.data(), environment.data(), nullptr, nullptr, nullptr, path.string().c_str(), lldb::eLaunchFlagNone, false, error));
  }

  if(error.Fail()) {
    Terminal::get().async_print(std::string("Error (debug): ") + error.GetCString() + '\n', true);
    for(auto &handler : on_exit)
      handler(-1);
    return;
  }

  if(debug_thread.joinable())
    debug_thread.join();
  for(auto &handler : on_start)
    handler(*process);

  for(auto &command : startup_commands) {
    lldb::SBCommandReturnObject command_return_object;
    debugger->GetCommandInterpreter().HandleCommand(command.c_str(), command_return_object, false);
  }

  debug_thread = std::thread([this]() {
    lldb::SBEvent event;
    while(true) {
      LockGuard lock(mutex);
      if(listener->GetNextEvent(event)) {
        if((event.GetType() & lldb::SBProcess::eBroadcastBitStateChanged) > 0) {
          auto state = process->GetStateFromEvent(event);
          this->state = state;

          if(state == lldb::StateType::eStateStopped) {
            for(uint32_t c = 0; c < process->GetNumThreads(); c++) {
              auto thread = process->GetThreadAtIndex(c);
              if(thread.GetStopReason() >= 2) {
                process->SetSelectedThreadByIndexID(thread.GetIndexID());
                break;
              }
            }
          }

          lock.unlock();
          for(auto &handler : on_event)
            handler(event);
          lock.lock();

          if(state == lldb::StateType::eStateExited || state == lldb::StateType::eStateCrashed) {
            auto exit_status = state == lldb::StateType::eStateCrashed ? -1 : process->GetExitStatus();
            lock.unlock();
            for(auto &handler : on_exit)
              handler(exit_status);
            lock.lock();
            process.reset();
            this->state = lldb::StateType::eStateInvalid;
            return;
          }
        }
        if((event.GetType() & lldb::SBProcess::eBroadcastBitSTDOUT) > 0) {
          char buffer[buffer_size];
          size_t n;
          while((n = process->GetSTDOUT(buffer, buffer_size)) != 0)
            Terminal::get().async_print(std::string(buffer, n));
        }
        //TODO: for some reason stderr is redirected to stdout
        if((event.GetType() & lldb::SBProcess::eBroadcastBitSTDERR) > 0) {
          char buffer[buffer_size];
          size_t n;
          while((n = process->GetSTDERR(buffer, buffer_size)) != 0)
            Terminal::get().async_print(std::string(buffer, n), true);
        }
      }
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  });
}

void Debug::LLDB::continue_debug() {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateStopped)
    process->Continue();
}

void Debug::LLDB::stop() {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateRunning) {
    auto error = process->Stop();
    if(error.Fail())
      Terminal::get().async_print(std::string("Error (debug): ") + error.GetCString() + '\n', true);
  }
}

void Debug::LLDB::kill() {
  LockGuard lock(mutex);
  if(process) {
    auto error = process->Kill();
    if(error.Fail())
      Terminal::get().async_print(std::string("Error (debug): ") + error.GetCString() + '\n', true);
  }
}

void Debug::LLDB::step_over() {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateStopped) {
    process->GetSelectedThread().StepOver();
  }
}

void Debug::LLDB::step_into() {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateStopped) {
    process->GetSelectedThread().StepInto();
  }
}

void Debug::LLDB::step_out() {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateStopped) {
    process->GetSelectedThread().StepOut();
  }
}

std::pair<std::string, std::string> Debug::LLDB::run_command(const std::string &command) {
  LockGuard lock(mutex);
  std::pair<std::string, std::string> command_return;
  if(state == lldb::StateType::eStateStopped || state == lldb::StateType::eStateRunning) {
    lldb::SBCommandReturnObject command_return_object;
    debugger->GetCommandInterpreter().HandleCommand(command.c_str(), command_return_object, true);
    auto output = command_return_object.GetOutput();
    if(output)
      command_return.first = output;
    auto error = command_return_object.GetError();
    if(error)
      command_return.second = error;
  }
  return command_return;
}

std::vector<Debug::LLDB::Frame> Debug::LLDB::get_backtrace() {
  LockGuard lock(mutex);
  std::vector<Frame> backtrace;
  if(state == lldb::StateType::eStateStopped) {
    auto thread = process->GetSelectedThread();
    for(uint32_t c_f = 0; c_f < thread.GetNumFrames(); c_f++) {
      Frame backtrace_frame;
      auto frame = thread.GetFrameAtIndex(c_f);

      backtrace_frame.index = c_f;

      if(auto function_name = frame.GetFunctionName())
        backtrace_frame.function_name = function_name;

      if(auto module_filename = frame.GetModule().GetFileSpec().GetFilename())
        backtrace_frame.module_filename = module_filename;

      auto line_entry = frame.GetLineEntry();
      if(line_entry.IsValid()) {
        lldb::SBStream stream;
        line_entry.GetFileSpec().GetDescription(stream);
        auto column = line_entry.GetColumn();
        if(column == 0)
          column = 1;
        backtrace_frame.file_path = filesystem::get_normal_path(stream.GetData());
        backtrace_frame.line_nr = line_entry.GetLine();
        backtrace_frame.line_index = column;
      }
      backtrace.emplace_back(backtrace_frame);
    }
  }
  return backtrace;
}

std::vector<Debug::LLDB::Variable> Debug::LLDB::get_variables() {
  LockGuard lock(mutex);
  std::vector<Debug::LLDB::Variable> variables;
  if(state == lldb::StateType::eStateStopped) {
    for(uint32_t c_t = 0; c_t < process->GetNumThreads(); c_t++) {
      auto thread = process->GetThreadAtIndex(c_t);
      for(uint32_t c_f = 0; c_f < thread.GetNumFrames(); c_f++) {
        auto frame = thread.GetFrameAtIndex(c_f);
        auto values = frame.GetVariables(true, true, true, false);
        for(uint32_t value_index = 0; value_index < values.GetSize(); value_index++) {
          lldb::SBStream stream;
          auto value = values.GetValueAtIndex(value_index);

          Debug::LLDB::Variable variable;
          variable.thread_index_id = thread.GetIndexID();
          variable.frame_index = c_f;
          if(auto name = value.GetName())
            variable.name = name;
          value.GetDescription(stream);
          variable.value = stream.GetData();

          auto declaration = value.GetDeclaration();
          if(declaration.IsValid()) {
            variable.declaration_found = true;
            variable.line_nr = declaration.GetLine();
            variable.line_index = declaration.GetColumn();
            if(variable.line_index == 0)
              variable.line_index = 1;

            auto file_spec = declaration.GetFileSpec();
            variable.file_path = filesystem::get_normal_path(file_spec.GetDirectory());
            variable.file_path /= file_spec.GetFilename();
          }
          else {
            variable.declaration_found = false;
            auto line_entry = frame.GetLineEntry();
            if(line_entry.IsValid()) {
              variable.line_nr = line_entry.GetLine();
              variable.line_index = line_entry.GetColumn();
              if(variable.line_index == 0)
                variable.line_index = 1;

              auto file_spec = line_entry.GetFileSpec();
              variable.file_path = filesystem::get_normal_path(file_spec.GetDirectory());
              variable.file_path /= file_spec.GetFilename();
            }
          }
          variables.emplace_back(variable);
        }
      }
    }
  }
  return variables;
}

void Debug::LLDB::select_frame(uint32_t frame_index, uint32_t thread_index_id) {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateStopped) {
    if(thread_index_id != 0)
      process->SetSelectedThreadByIndexID(thread_index_id);
    process->GetSelectedThread().SetSelectedFrame(frame_index);
    ;
  }
}

std::string Debug::LLDB::get_value(const std::string &variable, const boost::filesystem::path &file_path, unsigned int line_nr, unsigned int line_index) {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateStopped) {
    auto frame = process->GetSelectedThread().GetSelectedFrame();

    auto values = frame.GetVariables(true, true, true, false);
    //First try to find variable based on name, file and line number
    if(!file_path.empty()) {
      for(uint32_t value_index = 0; value_index < values.GetSize(); value_index++) {
        lldb::SBStream stream;
        auto value = values.GetValueAtIndex(value_index);

        if(auto name = value.GetName()) {
          if(name == variable) {
            auto declaration = value.GetDeclaration();
            if(declaration.IsValid()) {
              if(declaration.GetLine() == line_nr && (declaration.GetColumn() == 0 || declaration.GetColumn() == line_index)) {
                auto file_spec = declaration.GetFileSpec();
                auto value_decl_path = filesystem::get_normal_path(file_spec.GetDirectory());
                value_decl_path /= file_spec.GetFilename();
                if(value_decl_path == file_path) {
                  value.GetDescription(stream);
                  std::string variable_value = stream.GetData();
                  if(ends_with(variable_value, "\n\n"))
                    variable_value.pop_back(); // Remove newline at end of string
                  return variable_value;
                }
              }
            }
          }
        }
      }
    }
  }
  return {};
}

std::string Debug::LLDB::get_value(const std::string &expression) {
  LockGuard lock(mutex);
  std::string variable_value;
  if(state == lldb::StateType::eStateStopped) {
    auto frame = process->GetSelectedThread().GetSelectedFrame();
    if(variable_value.empty()) {
      // Attempt to get variable from variable expression, using the faster GetValueForVariablePath
      auto value = frame.GetValueForVariablePath(expression.c_str());
      if(value.IsValid()) {
        lldb::SBStream stream;
        value.GetDescription(stream);
        variable_value = stream.GetData();
      }
    }
    if(variable_value.empty()) {
      // Attempt to get variable from variable expression, using the slower EvaluateExpression
      auto value = frame.EvaluateExpression(expression.c_str());
      if(value.IsValid() && !value.GetError().Fail()) {
        lldb::SBStream stream;
        value.GetDescription(stream);
        variable_value = stream.GetData();
      }
    }
  }
  return variable_value;
}

std::string Debug::LLDB::get_return_value(const boost::filesystem::path &file_path, unsigned int line_nr, unsigned int line_index) {
  LockGuard lock(mutex);
  std::string return_value;
  if(state == lldb::StateType::eStateStopped) {
    auto thread = process->GetSelectedThread();
    auto thread_return_value = thread.GetStopReturnValue();
    if(thread_return_value.IsValid()) {
      auto line_entry = thread.GetSelectedFrame().GetLineEntry();
      if(line_entry.IsValid()) {
        lldb::SBStream stream;
        line_entry.GetFileSpec().GetDescription(stream);
        if(filesystem::get_normal_path(stream.GetData()) == file_path && line_entry.GetLine() == line_nr &&
           (line_entry.GetColumn() == 0 || line_entry.GetColumn() == line_index)) {
          lldb::SBStream stream;
          thread_return_value.GetDescription(stream);
          return_value = stream.GetData();
        }
      }
    }
  }
  return return_value;
}

bool Debug::LLDB::is_invalid() {
  LockGuard lock(mutex);
  return state == lldb::StateType::eStateInvalid;
}

bool Debug::LLDB::is_stopped() {
  LockGuard lock(mutex);
  return state == lldb::StateType::eStateStopped;
}

bool Debug::LLDB::is_running() {
  LockGuard lock(mutex);
  return state == lldb::StateType::eStateRunning;
}

void Debug::LLDB::add_breakpoint(const boost::filesystem::path &file_path, int line_nr) {
  LockGuard lock(mutex);
  if(state == lldb::eStateStopped || state == lldb::eStateRunning) {
    if(!(process->GetTarget().BreakpointCreateByLocation(file_path.string().c_str(), line_nr)).IsValid())
      Terminal::get().async_print("Error (debug): Could not create breakpoint at: " + file_path.string() + ":" + std::to_string(line_nr) + '\n', true);
  }
}

void Debug::LLDB::remove_breakpoint(const boost::filesystem::path &file_path, int line_nr, int line_count) {
  LockGuard lock(mutex);
  if(state == lldb::eStateStopped || state == lldb::eStateRunning) {
    auto target = process->GetTarget();
    for(int line_nr_try = line_nr; line_nr_try < line_count; line_nr_try++) {
      for(uint32_t b_index = 0; b_index < target.GetNumBreakpoints(); b_index++) {
        auto breakpoint = target.GetBreakpointAtIndex(b_index);
        for(uint32_t l_index = 0; l_index < breakpoint.GetNumLocations(); l_index++) {
          auto line_entry = breakpoint.GetLocationAtIndex(l_index).GetAddress().GetLineEntry();
          if(line_entry.GetLine() == static_cast<uint32_t>(line_nr_try)) {
            auto file_spec = line_entry.GetFileSpec();
            auto breakpoint_path = filesystem::get_normal_path(file_spec.GetDirectory());
            breakpoint_path /= file_spec.GetFilename();
            if(breakpoint_path == file_path) {
              if(!target.BreakpointDelete(breakpoint.GetID()))
                Terminal::get().async_print("Error (debug): Could not delete breakpoint at: " + file_path.string() + ":" + std::to_string(line_nr) + '\n', true);
              return;
            }
          }
        }
      }
    }
  }
}

void Debug::LLDB::write(const std::string &buffer) {
  LockGuard lock(mutex);
  if(state == lldb::StateType::eStateRunning) {
    process->PutSTDIN(buffer.c_str(), buffer.size());
  }
}

void Debug::LLDB::init_module(pybind11::module &api) {
  py::class_<Debug::LLDB, std::unique_ptr<Debug::LLDB, py::nodelete>> dbg(api, "LLDB");
  py::class_<Debug::LLDB::Frame>(dbg, "Frame")
      .def_readwrite("index", &Debug::LLDB::Frame::index)
      .def_readwrite("module_filename", &Debug::LLDB::Frame::module_filename)
      .def_readwrite("file_path", &Debug::LLDB::Frame::file_path)
      .def_readwrite("function_name", &Debug::LLDB::Frame::function_name)
      .def_readwrite("line_nr", &Debug::LLDB::Frame::line_nr)
      .def_readwrite("line_index", &Debug::LLDB::Frame::line_index)

      ;
  py::class_<Debug::LLDB::Variable>(dbg, "Variable")
      .def_readwrite("thread_index_id", &Debug::LLDB::Variable::thread_index_id)
      .def_readwrite("frame_index", &Debug::LLDB::Variable::frame_index)
      .def_readwrite("name", &Debug::LLDB::Variable::name)
      .def_readwrite("value", &Debug::LLDB::Variable::value)
      .def_readwrite("declaration_found", &Debug::LLDB::Variable::declaration_found)
      .def_readwrite("file_path", &Debug::LLDB::Variable::file_path)
      .def_readwrite("line_nr", &Debug::LLDB::Variable::line_nr)
      .def_readwrite("line_index", &Debug::LLDB::Variable::line_index)

      ;

  // py::class_<lldb::SBProcess>(api, "SBProcess");
  // .def_readwrite("on_start", &Debug::LLDB::on_start)
  // .def_readwrite("on_event", &Debug::LLDB::on_event)

  dbg
      .def(py::init([]() { return &(Debug::LLDB::get()); }))
      .def_readwrite("on_exit", &Debug::LLDB::on_exit)
      .def("continue_debug", &Debug::LLDB::continue_debug)
      .def("stop", &Debug::LLDB::stop)
      .def("kill", &Debug::LLDB::kill)
      .def("step_over", &Debug::LLDB::step_over)
      .def("step_into", &Debug::LLDB::step_into)
      .def("step_out", &Debug::LLDB::step_out)
      .def("is_invalid", &Debug::LLDB::is_invalid)
      .def("is_stopped", &Debug::LLDB::is_stopped)
      .def("is_running", &Debug::LLDB::is_running)
      .def("get_backtrace", &Debug::LLDB::get_backtrace)
      .def("get_variables", &Debug::LLDB::get_variables)
      .def("start", &Debug::LLDB::start,
           py::arg("command"),
           py::arg("path") = "",
           py::arg("breakpoints") = std::vector<std::pair<boost::filesystem::path, int>>(),
           py::arg("startup_commands") = std::vector<std::string>(),
           py::arg("remote_host") = "")
      .def("run_command", &Debug::LLDB::run_command,
           py::arg("command"))
      .def("select_frame", &Debug::LLDB::select_frame,
           py::arg("frame_index"),
           py::arg("thread_index_id") = 0)
      .def("get_return_value", &Debug::LLDB::get_return_value,
           py::arg("file_path"),
           py::arg("line_nr"),
           py::arg("line_index"))
      .def("add_breakpoint", &Debug::LLDB::add_breakpoint,
           py::arg("file_path"),
           py::arg("line_nr"))
      .def("remove_breakpoint", &Debug::LLDB::remove_breakpoint,
           py::arg("file_path"),
           py::arg("line_nr"),
           py::arg("line_count"))
      .def("write", &Debug::LLDB::write,
           py::arg("buffer"))

      ;
}
