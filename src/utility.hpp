#pragma once
#include <functional>
#include <string>

class ScopeGuard {
public:
  std::function<void()> on_exit;
  ~ScopeGuard();
};

/// Returns number of utf8 characters in text argument
size_t utf8_character_count(const std::string &text, size_t pos = 0, size_t length = std::string::npos) noexcept;

bool starts_with(const char *str, const std::string &test) noexcept;
bool starts_with(const char *str, const char *test) noexcept;
bool starts_with(const std::string &str, const std::string &test) noexcept;
bool starts_with(const std::string &str, const char *test) noexcept;
/// Comparison starts from position pos in str
bool starts_with(const std::string &str, size_t pos, const std::string &test) noexcept;
/// Comparison starts from position pos in str
bool starts_with(const std::string &str, size_t pos, const char *test) noexcept;

bool ends_with(const std::string &str, const std::string &test) noexcept;
bool ends_with(const std::string &str, const char *test) noexcept;
