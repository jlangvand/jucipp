#include "utility.hpp"
#include <cstring>

ScopeGuard::~ScopeGuard() {
  if(on_exit)
    on_exit();
}

size_t utf8_character_count(const std::string &text) noexcept {
  size_t count = 0;
  for(auto chr : text) {
    if(static_cast<unsigned char>(chr) <= 0b01111111 || static_cast<unsigned char>(chr) >= 0b11000000)
      ++count;
  }
  return count;
}

bool starts_with(const char *str, const std::string &test) noexcept {
  for(size_t i = 0; i < test.size(); ++i) {
    if(*str == '\0')
      return false;
    if(*str != test[i])
      return false;
    ++str;
  }
  return true;
}

bool starts_with(const char *str, const char *test) noexcept {
  for(; *test != '\0'; ++test) {
    if(*str == '\0')
      return false;
    if(*str != *test)
      return false;
    ++str;
  }
  return true;
}

bool starts_with(const std::string &str, const std::string &test) noexcept {
  return str.compare(0, test.size(), test) == 0;
}

bool starts_with(const std::string &str, const char *test) noexcept {
  for(size_t i = 0; i < str.size(); ++i) {
    if(*test == '\0')
      return true;
    if(str[i] != *test)
      return false;
    ++test;
  }
  return *test == '\0';
}

bool starts_with(const std::string &str, size_t pos, const std::string &test) noexcept {
  if(pos > str.size())
    return false;
  return str.compare(pos, test.size(), test) == 0;
}

bool starts_with(const std::string &str, size_t pos, const char *test) noexcept {
  if(pos > str.size())
    return false;
  for(size_t i = pos; i < str.size(); ++i) {
    if(*test == '\0')
      return true;
    if(str[i] != *test)
      return false;
    ++test;
  }
  return *test == '\0';
}

bool ends_with(const std::string &str, const std::string &test) noexcept {
  if(test.size() > str.size())
    return false;
  return str.compare(str.size() - test.size(), test.size(), test) == 0;
}

bool ends_with(const std::string &str, const char *test) noexcept {
  auto test_size = strlen(test);
  if(test_size > str.size())
    return false;
  return str.compare(str.size() - test_size, test_size, test) == 0;
}
