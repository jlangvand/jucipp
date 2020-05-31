#pragma once
#include <boost/filesystem.hpp>
#include <sstream>
#include <string>
#include <vector>

class Ctags {
public:
  class Location {
  public:
    boost::filesystem::path file_path;
    unsigned long line;
    unsigned long index;
    std::string symbol;
    std::string scope;
    std::string source;
    std::string kind;
    operator bool() const { return !file_path.empty(); }
  };

  Ctags(const boost::filesystem::path &path, bool enable_scope = false, bool enable_kind = false);

  operator bool();

  Location get_location(const std::string &line, bool add_markup = false) const;

  boost::filesystem::path project_path;
  std::stringstream output;

  static std::vector<Location> get_locations(const boost::filesystem::path &path, const std::string &name, const std::string &type);

private:
  bool enable_scope, enable_kind;
  static std::vector<std::string> get_type_parts(const std::string &type);
};
