#pragma once

#include <regex>
#include <string>
#include <vector>

class Snippets {
  Snippets();

public:
  class Snippet {
  public:
    std::string prefix;
    std::string body;
    std::string description;
  };

  static Snippets &get() {
    static Snippets singleton;
    return singleton;
  }

  std::vector<std::pair<std::regex, std::vector<Snippet>>> snippets;
  void load();
};