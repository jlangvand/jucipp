#pragma once
#include <boost/filesystem.hpp>

class Grep {
public:
  class Location {
  public:
    std::string file_path;
    unsigned long line;
    unsigned long offset;
    std::string markup;
    operator bool() const { return !file_path.empty(); }
  };

  static std::pair<boost::filesystem::path, std::unique_ptr<std::stringstream>> get_result(const boost::filesystem::path &path, const std::string &pattern, bool case_sensitive, bool extended_regex);

  static Location get_location(std::string line, bool color_codes_to_markup, bool include_offset, const std::string &only_for_file = {});
};