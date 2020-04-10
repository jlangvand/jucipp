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
    char kind;
    operator bool() const { return !file_path.empty(); }
  };

  static std::pair<boost::filesystem::path, std::unique_ptr<std::stringstream>> get_result(const boost::filesystem::path &path);

  static Location get_location(const std::string &line, bool add_markup);

  static std::vector<Location> get_locations(const boost::filesystem::path &path, const std::string &name, const std::string &type);

private:
  static std::vector<std::string> get_type_parts(const std::string &type);
};
