#pragma once
#include <boost/filesystem.hpp>
#include <list>
#include <map>
#include <vector>
#include "python_bind.h"

class CMake {
public:
  CMake(const boost::filesystem::path &path);
  boost::filesystem::path project_path;
  bool update_default_build(const boost::filesystem::path &default_build_path, bool force = false);
  bool update_debug_build(const boost::filesystem::path &debug_build_path, bool force = false);
  boost::filesystem::path get_executable(const boost::filesystem::path &build_path, const boost::filesystem::path &file_path);
  static void init_module(py::module &api);

private:
  std::vector<boost::filesystem::path> paths;

  struct Function {
    std::string name;
    std::list<std::string> parameters;
  };
  static void parse_file(const std::string &src, std::map<std::string, std::list<std::string>> &variables, std::function<void(Function &&)> &&on_function);
};
