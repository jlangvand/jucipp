#pragma once
#include <functional>
#include <string>

class ScopeGuard {
public:
  std::function<void()> on_exit;
  ~ScopeGuard();
};

bool starts_with(const char *str, const std::string &test) noexcept;
bool starts_with(const char *str, const char *test) noexcept;
bool starts_with(const std::string &str, const std::string &test) noexcept;
bool starts_with(const std::string &str, const char *test) noexcept;
bool starts_with(const std::string &str, size_t pos, const std::string &test) noexcept;
bool starts_with(const std::string &str, size_t pos, const char *test) noexcept;

bool ends_with(const std::string &str, const std::string &test) noexcept;
bool ends_with(const std::string &str, const char *test) noexcept;
