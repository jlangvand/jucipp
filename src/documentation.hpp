#pragma once
#include "python_bind.h"
#include <string>
#include <vector>

namespace Documentation {
  class CppReference {
  public:
    static std::vector<std::string> get_headers(const std::string &symbol) noexcept;
    static std::string get_url(const std::string &symbol) noexcept;
    static void init_module(py::module &api);
  };
} // namespace Documentation
