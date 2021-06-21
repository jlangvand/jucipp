#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

void JSON::write_json_internal(std::ostream &stream, const boost::property_tree::ptree &pt, bool pretty, const std::vector<std::string> &not_string_keys, size_t column, const std::string &key) {
  // Based on boost::property_tree::json_parser::write_json_helper()

  if(pt.empty()) { // Value
    auto value = pt.get_value<std::string>();
    if(std::any_of(not_string_keys.begin(), not_string_keys.end(), [&key](const std::string &not_string_key) {
         return key == not_string_key;
       }))
      stream << value;
    else
      stream << '"' << escape_string(value) << '"';
  }
  else if(pt.count(std::string{}) == pt.size()) { // Array
    stream << '[';
    if(pretty)
      stream << '\n';

    for(auto it = pt.begin(); it != pt.end(); ++it) {
      if(pretty)
        stream << std::string((column + 1) * 2, ' ');

      write_json_internal(stream, it->second, pretty, not_string_keys, column + 1, key);

      if(std::next(it) != pt.end())
        stream << ',';
      if(pretty)
        stream << '\n';
    }

    if(pretty)
      stream << std::string(column * 2, ' ');
    stream << ']';
  }
  else { // Object
    stream << '{';
    if(pretty)
      stream << '\n';

    for(auto it = pt.begin(); it != pt.end(); ++it) {
      if(pretty)
        stream << std::string((column + 1) * 2, ' ');

      stream << '"' << escape_string(it->first) << "\":";
      if(pretty)
        stream << ' ';
      write_json_internal(stream, it->second, pretty, not_string_keys, column + 1, it->first);

      if(std::next(it) != pt.end())
        stream << ',';
      if(pretty)
        stream << '\n';
    }

    if(pretty)
      stream << std::string(column * 2, ' ');
    stream << '}';
  }
}
void JSON::write(std::ostream &stream, const boost::property_tree::ptree &pt, bool pretty, const std::vector<std::string> &not_string_keys) {
  write_json_internal(stream, pt, pretty, not_string_keys, 0, "");
}

std::string JSON::escape_string(std::string string) {
  for(size_t c = 0; c < string.size(); ++c) {
    if(string[c] == '\b') {
      string.replace(c, 1, "\\b");
      ++c;
    }
    else if(string[c] == '\f') {
      string.replace(c, 1, "\\f");
      ++c;
    }
    else if(string[c] == '\n') {
      string.replace(c, 1, "\\n");
      ++c;
    }
    else if(string[c] == '\r') {
      string.replace(c, 1, "\\r");
      ++c;
    }
    else if(string[c] == '\t') {
      string.replace(c, 1, "\\t");
      ++c;
    }
    else if(string[c] == '"') {
      string.replace(c, 1, "\\\"");
      ++c;
    }
    else if(string[c] == '\\') {
      string.replace(c, 1, "\\\\");
      ++c;
    }
  }
  return string;
}
