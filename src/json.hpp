#pragma once

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <ostream>

class JSON {
  static void write_json_internal(std::ostream &stream, const boost::property_tree::ptree &pt, bool pretty, const std::vector<std::string> &not_string_keys, size_t column, const std::string &key);

public:
  /// A replacement of boost::property_tree::write_json() as it does not conform to the JSON standard (https://svn.boost.org/trac10/ticket/9721).
  /// Some JSON parses expects, for instance, number types instead of numbers in strings.
  /// Note that boost::property_tree::write_json() will always produce string values.
  /// Use not_string_keys to specify which keys that should not have string values.
  /// TODO: replace boost::property_tree with another JSON library.
  static void write(std::ostream &stream, const boost::property_tree::ptree &pt, bool pretty = true, const std::vector<std::string> &not_string_keys = {});
  /// A replacement of boost::property_tree::escape_text() as it does not conform to the JSON standard.
  static std::string escape_string(std::string string);
};
