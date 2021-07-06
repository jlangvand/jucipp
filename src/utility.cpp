#include "utility.hpp"
#include <algorithm>
#include <cstring>

ScopeGuard::~ScopeGuard() {
  if(on_exit)
    on_exit();
}

size_t utf8_character_count(const std::string &text, size_t pos, size_t length) noexcept {
  size_t characters = 0;
  auto size = length == std::string::npos ? text.size() : std::min(pos + length, text.size());
  for(; pos < size;) {
    if(static_cast<unsigned char>(text[pos]) <= 0b01111111) {
      ++characters;
      ++pos;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11111000) // Invalid UTF-8 byte
      ++pos;
    else if(static_cast<unsigned char>(text[pos]) >= 0b11110000) {
      ++characters;
      pos += 4;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11100000) {
      ++characters;
      pos += 3;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11000000) {
      ++characters;
      pos += 2;
    }
    else // // Invalid start of UTF-8 character
      ++pos;
  }
  return characters;
}

size_t utf16_code_units_byte_count(const std::string &text, size_t code_units, size_t start_pos) {
  if(code_units == 0)
    return 0;

  size_t pos = start_pos;
  size_t current_code_units = 0;
  for(; pos < text.size();) {
    if(static_cast<unsigned char>(text[pos]) <= 0b01111111) {
      ++current_code_units;
      ++pos;
      if(current_code_units >= code_units)
        break;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11111000) // Invalid UTF-8 byte
      ++pos;
    else if(static_cast<unsigned char>(text[pos]) >= 0b11110000) {
      current_code_units += 2;
      pos += 4;
      if(current_code_units >= code_units)
        break;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11100000) {
      ++current_code_units;
      pos += 3;
      if(current_code_units >= code_units)
        break;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11000000) {
      ++current_code_units;
      pos += 2;
      if(current_code_units >= code_units)
        break;
    }
    else // // Invalid start of UTF-8 character
      ++pos;
  }
  return pos - start_pos;
}

size_t utf16_code_unit_count(const std::string &text, size_t pos, size_t length) {
  size_t code_units = 0;
  auto size = length == std::string::npos ? text.size() : std::min(pos + length, text.size());
  for(; pos < size;) {
    if(static_cast<unsigned char>(text[pos]) <= 0b01111111) {
      ++code_units;
      ++pos;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11111000) // Invalid UTF-8 byte
      ++pos;
    else if(static_cast<unsigned char>(text[pos]) >= 0b11110000) {
      code_units += 2;
      pos += 4;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11100000) {
      ++code_units;
      pos += 3;
    }
    else if(static_cast<unsigned char>(text[pos]) >= 0b11000000) {
      ++code_units;
      pos += 2;
    }
    else // // Invalid start of UTF-8 character
      ++pos;
  }
  return code_units;
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

std::string escape(const std::string &input, const std::set<char> &escape_chars) {
  std::string result;
  result.reserve(input.size());
  for(auto &chr : input) {
    if(escape_chars.find(chr) != escape_chars.end())
      result += '\\';
    result += chr;
  }
  return result;
}

std::string to_hex_string(const std::string &input) {
  std::string result;
  result.reserve(input.size() * 2);
  std::string hex_chars = "0123456789abcdef";
  for(auto &chr : input) {
    result += hex_chars[static_cast<unsigned char>(chr) >> 4];
    result += hex_chars[static_cast<unsigned char>(chr) & 0x0f];
  }
  return result;
}
