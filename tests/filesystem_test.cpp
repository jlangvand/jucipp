#include "filesystem.hpp"
#include <glib.h>

int main() {
  {
    auto home_path = filesystem::get_home_path();
    g_assert(!home_path.empty());
  }

  {
    auto original = "test () '\"";
    auto escaped = filesystem::escape_argument(original);
    g_assert_cmpstr(escaped.c_str(), ==, "test\\ \\(\\)\\ \\'\\\"");
    auto unescaped = filesystem::unescape_argument(escaped);
    g_assert_cmpstr(unescaped.c_str(), ==, original);
  }
  {
    auto unescaped = filesystem::unescape_argument("'test \\()\"\\''");
    g_assert_cmpstr(unescaped.c_str(), ==, "test \\()\"'");
  }
  {
    auto unescaped = filesystem::unescape_argument("\"test \\'()\\\"\"");
    g_assert_cmpstr(unescaped.c_str(), ==, "test \\'()\"");
  }
  {
    auto unescaped = filesystem::unescape_argument("\\\\");
    g_assert_cmpstr(unescaped.c_str(), ==, "\\");
  }
  {
    auto unescaped = filesystem::unescape_argument("\\\\\\ ");
    g_assert_cmpstr(unescaped.c_str(), ==, "\\ ");
  }
  {
    auto unescaped = filesystem::unescape_argument("\\\\\\ \\ \\ \\\\");
    g_assert_cmpstr(unescaped.c_str(), ==, "\\   \\");
  }
  {
    auto unescaped = filesystem::unescape_argument("c:\\a\\ b\\c");
    g_assert_cmpstr(unescaped.c_str(), ==, "c:\\a b\\c");
  }
  {
    auto unescaped = filesystem::unescape_argument("\"\\\\\\\"\"");
    g_assert_cmpstr(unescaped.c_str(), ==, "\\\"");
  }
  {
    auto unescaped = filesystem::unescape_argument("\"\\\"\"");
    g_assert_cmpstr(unescaped.c_str(), ==, "\"");
  }
  {
    auto unescaped = filesystem::unescape_argument("\"a\\b\"");
    g_assert_cmpstr(unescaped.c_str(), ==, "a\\b");
  }

  auto tests_path = boost::filesystem::canonical(JUCI_TESTS_PATH);
  {
    g_assert(filesystem::file_in_path(tests_path / "filesystem_test.cc", tests_path));
    g_assert(!filesystem::file_in_path(boost::filesystem::canonical(tests_path / ".." / "CMakeLists.txt"), tests_path));
  }

  auto license_file = boost::filesystem::canonical(tests_path / ".." / "LICENSE");
  {
    g_assert(filesystem::find_file_in_path_parents("LICENSE", tests_path) == license_file);
  }

  {
    g_assert(filesystem::get_normal_path(tests_path / ".." / "LICENSE") == license_file);
    g_assert(filesystem::get_normal_path("/foo") == "/foo");
    g_assert(filesystem::get_normal_path("/foo/") == "/foo");
    g_assert(filesystem::get_normal_path("/foo/.") == "/foo");
    g_assert(filesystem::get_normal_path("/foo/./bar/..////") == "/foo");
    g_assert(filesystem::get_normal_path("/foo/.///bar/../") == "/foo");
    g_assert(filesystem::get_normal_path("../foo") == "../foo");
    g_assert(filesystem::get_normal_path("../../foo") == "../../foo");
    g_assert(filesystem::get_normal_path("../././foo") == "../foo");
    g_assert(filesystem::get_normal_path("/../foo") == "/../foo");
  }

  {
    boost::filesystem::path relative_path = "filesystem_test.cc";
    g_assert(filesystem::get_relative_path(tests_path / relative_path, tests_path) == relative_path);
    g_assert(filesystem::get_relative_path(tests_path / "test" / relative_path, tests_path) == boost::filesystem::path("test") / relative_path);

    g_assert(filesystem::get_relative_path("/test/test/test.cc", "/test/base") == boost::filesystem::path("..") / "test" / "test.cc");
    g_assert(filesystem::get_relative_path("/test/test/test/test.cc", "/test/base") == boost::filesystem::path("..") / "test" / "test" / "test.cc");
    g_assert(filesystem::get_relative_path("/test2/test.cc", "/test/base") == boost::filesystem::path("..") / ".." / "test2" / "test.cc");
  }

  {
    boost::filesystem::path path = "/ro ot/te stæøå.txt";
    auto uri = filesystem::get_uri_from_path(path);
    g_assert(uri == "file:///ro%20ot/te%20st%C3%A6%C3%B8%C3%A5.txt");
    g_assert(path == filesystem::get_path_from_uri(uri));
  }

  {
    auto path = filesystem::get_cache_path();
    path = filesystem::get_short_path(path);
    g_assert(path == "~/.cache");
  }

  {
    auto path = filesystem::get_data_path();
    path = filesystem::get_short_path(path);
    g_assert(path == "~/.local/share");
  }

  {
    auto path = filesystem::get_config_path();
    path = filesystem::get_short_path(path);
    g_assert(path == "~/.config");
  }
}
