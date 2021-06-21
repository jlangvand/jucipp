#include "json.hpp"
#include <glib.h>
#include <iostream>
#include <sstream>

int main() {
  std::string json = R"({
  "integer": 3,
  "integer_as_string": "3",
  "string": "some\ntext",
  "string2": "1test",
  "array": [
    1,
    3,
    3.14
  ],
  "array_with_strings": [
    "a",
    "b",
    "c"
  ],
  "object": {
    "integer": 3,
    "string": "some\ntext",
    "array": [
      1,
      3,
      3.14
    ]
  }
})";
  {
    std::istringstream istream(json);

    boost::property_tree::ptree pt;
    boost::property_tree::read_json(istream, pt);

    std::ostringstream ostream;
    JSON::write(ostream, pt, true, {"integer", "array"});
    g_assert(ostream.str() == json);
  }
  {
    std::istringstream istream(json);

    boost::property_tree::ptree pt;
    boost::property_tree::read_json(istream, pt);

    std::ostringstream ostream;
    JSON::write(ostream, pt, false, {"integer", "array"});

    std::string non_pretty;
    for(auto &chr : json) {
      if(chr != ' ' && chr != '\n')
        non_pretty += chr;
    }
    g_assert(ostream.str() == non_pretty);
  }
}
