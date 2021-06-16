#include "utility.hpp"
#include <glib.h>

int main() {
  static bool scope_exit = false;
  {
    ScopeGuard guard{[] {
      scope_exit = true;
    }};
    g_assert(!scope_exit);
  }
  g_assert(scope_exit);

  g_assert(utf8_character_count("") == 0);
  g_assert(utf8_character_count("test") == 4);
  g_assert(utf8_character_count("æøå") == 3);
  g_assert(utf8_character_count("æøåtest") == 7);

  g_assert_cmpuint(utf16_code_units_byte_count("", 0), ==, 0);
  g_assert_cmpuint(utf16_code_units_byte_count("", 1), ==, 0);
  g_assert_cmpuint(utf16_code_units_byte_count("test", 0), ==, 0);
  g_assert_cmpuint(utf16_code_units_byte_count("test", 1), ==, 1);
  g_assert_cmpuint(utf16_code_units_byte_count("test", 3), ==, 3);
  g_assert_cmpuint(utf16_code_units_byte_count("test", 4), ==, 4);
  g_assert_cmpuint(utf16_code_units_byte_count("test", 5), ==, 4);

  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 0), ==, 0);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 1), ==, 2);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 2), ==, 4);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 3), ==, 6);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 4), ==, 6);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 5), ==, 6);

  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 0, 2), ==, 0);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 1, 2), ==, 2);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 2, 2), ==, 4);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 3, 2), ==, 4);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 1, 6), ==, 0);
  g_assert_cmpuint(utf16_code_units_byte_count("æøå", 0, 6), ==, 0);

  g_assert_cmpuint(strlen("🔥"), ==, 4); // Fire emoji

  g_assert_cmpuint(utf16_code_units_byte_count("🔥", 0), ==, 0);           // Fire emoji
  g_assert_cmpuint(utf16_code_units_byte_count("🔥", 2), ==, 4);           // Fire emoji
  g_assert_cmpuint(utf16_code_units_byte_count("🔥", 3), ==, 4);           // Fire emoji
  g_assert_cmpuint(utf16_code_units_byte_count("test🔥test", 0), ==, 0);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_units_byte_count("test🔥test", 4), ==, 4);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_units_byte_count("test🔥test", 6), ==, 8);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_units_byte_count("test🔥test", 7), ==, 9);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_units_byte_count("test🔥test", 10), ==, 12); // Fire emoji between test words
  g_assert_cmpuint(utf16_code_units_byte_count("test🔥test", 11), ==, 12); // Fire emoji between test words

  g_assert_cmpuint(utf16_code_unit_count("", 0, 0), ==, 0);
  g_assert_cmpuint(utf16_code_unit_count("", 0, 2), ==, 0);
  g_assert_cmpuint(utf16_code_unit_count("", 2, 2), ==, 0);
  g_assert_cmpuint(utf16_code_unit_count("test", 0, 1), ==, 1);
  g_assert_cmpuint(utf16_code_unit_count("test", 0, 4), ==, 4);
  g_assert_cmpuint(utf16_code_unit_count("test", 0, 10), ==, 4);
  g_assert_cmpuint(utf16_code_unit_count("test", 2, 2), ==, 2);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 0, 0), ==, 0);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 0, 2), ==, 1);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 0, 4), ==, 2);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 0, 6), ==, 3);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 2, 6), ==, 2);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 4, 6), ==, 1);
  g_assert_cmpuint(utf16_code_unit_count("æøå", 6, 6), ==, 0);
  g_assert_cmpuint(utf16_code_unit_count("test🔥test", 0, 0), ==, 0);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_unit_count("test🔥test", 0, 4), ==, 4);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_unit_count("test🔥test", 0, 8), ==, 6);   // Fire emoji between test words
  g_assert_cmpuint(utf16_code_unit_count("test🔥test", 0, 12), ==, 10); // Fire emoji between test words

  std::string empty;
  std::string test("test");
  std::string testtest("testtest");

  g_assert(starts_with("", empty));
  g_assert(starts_with("", ""));
  g_assert(starts_with(empty, ""));
  g_assert(starts_with(empty, empty));
  g_assert(starts_with(empty, 0, ""));
  g_assert(starts_with(empty, 0, empty));
  g_assert(ends_with(empty, ""));
  g_assert(ends_with(empty, empty));

  g_assert(starts_with(test.c_str(), empty));
  g_assert(starts_with(test.c_str(), ""));
  g_assert(starts_with(test, ""));
  g_assert(starts_with(test, empty));
  g_assert(starts_with(test, 0, ""));
  g_assert(starts_with(test, 0, empty));
  g_assert(ends_with(test, ""));
  g_assert(ends_with(test, empty));

  g_assert(!starts_with(empty, 10, ""));
  g_assert(!starts_with(empty, 10, empty));

  g_assert(!starts_with(test, 10, ""));
  g_assert(!starts_with(test, 10, empty));

  g_assert(!starts_with(test, 10, test.c_str()));
  g_assert(!starts_with(test, 10, test));

  g_assert(starts_with(test, 2, test.c_str() + 2));
  g_assert(starts_with(test, 2, test.substr(2)));

  g_assert(ends_with(test, test.c_str() + 2));
  g_assert(ends_with(test, test.substr(2)));

  g_assert(starts_with(test.c_str(), test));
  g_assert(starts_with(test.c_str(), test.c_str()));
  g_assert(starts_with(test, test.c_str()));
  g_assert(starts_with(test, test));
  g_assert(starts_with(test, 0, test.c_str()));
  g_assert(starts_with(test, 0, test));
  g_assert(ends_with(test, test.c_str()));
  g_assert(ends_with(test, test));

  g_assert(starts_with(testtest.c_str(), test));
  g_assert(starts_with(testtest.c_str(), test.c_str()));
  g_assert(starts_with(testtest, test.c_str()));
  g_assert(starts_with(testtest, test));
  g_assert(starts_with(testtest, 0, test.c_str()));
  g_assert(starts_with(testtest, 0, test));
  g_assert(ends_with(testtest, test.c_str()));
  g_assert(ends_with(testtest, test));
  g_assert(ends_with(testtest, "ttest"));
  g_assert(ends_with(testtest, std::string("ttest")));

  g_assert(!starts_with(test.c_str(), testtest));
  g_assert(!starts_with(test.c_str(), testtest.c_str()));
  g_assert(!starts_with(test, testtest.c_str()));
  g_assert(!starts_with(test, testtest));
  g_assert(!starts_with(test, 0, testtest.c_str()));
  g_assert(!starts_with(test, 0, testtest));
  g_assert(!ends_with(test, testtest.c_str()));
  g_assert(!ends_with(test, testtest));

  g_assert(!starts_with(empty.c_str(), test));
  g_assert(!starts_with(empty.c_str(), test.c_str()));
  g_assert(!starts_with(empty, test.c_str()));
  g_assert(!starts_with(empty, test));
  g_assert(!starts_with(empty, 0, test.c_str()));
  g_assert(!starts_with(empty, 0, test));
  g_assert(!ends_with(empty, test.c_str()));
  g_assert(!ends_with(empty, test));
}