#include "config.h"
#include "ctags.h"
#include "grep.h"
#include <glib.h>
#include <gtkmm.h>

void ctags_grep_test_function() {
}

int main() {
  auto app = Gtk::Application::create();
  Config::get().project.ctags_command = "ctags";
  Config::get().project.grep_command = "grep";
  Config::get().project.debug_build_path = "build";

  auto tests_path = boost::filesystem::canonical(JUCI_TESTS_PATH);

  // Ctags tests
  {
    auto result = Ctags::get_result(tests_path);
    g_assert(result.first == tests_path.parent_path());
    bool found = false;
    std::string line;
    while(std::getline(*result.second, line)) {
      if(line.find("ctags_grep_test_function") != std::string::npos) {
        {
          auto location = Ctags::get_location(line, false);
          g_assert(location.source == "void ctags_grep_test_function() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.index, ==, 5);
          g_assert(location.symbol == "ctags_grep_test_function");
          g_assert(location.scope.empty());
          g_assert(location.kind.empty());
        }
        {
          auto location = Ctags::get_location(line, true);
          g_assert(location.source == "void <b>ctags_grep_test_function</b>() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.index, ==, 5);
          g_assert(location.symbol == "ctags_grep_test_function");
          g_assert(location.scope.empty());
          g_assert(location.kind.empty());
        }
        found = true;
        break;
      }
    }
    g_assert(found == true);
  }
  {
    auto result = Ctags::get_result(tests_path, true);
    g_assert(result.first == tests_path.parent_path());
    bool found = false;
    std::string line;
    while(std::getline(*result.second, line)) {
      if(line.find("ctags_grep_test_function") != std::string::npos) {
        {
          auto location = Ctags::get_location(line, false, true);
          g_assert(location.source == "void ctags_grep_test_function() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.index, ==, 5);
          g_assert(location.symbol == "ctags_grep_test_function");
          g_assert(location.scope.empty());
          g_assert(location.kind == "function");
        }
        {
          auto location = Ctags::get_location(line, true, true);
          g_assert(location.source == "void <b>ctags_grep_test_function</b>() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.index, ==, 5);
          g_assert(location.symbol == "ctags_grep_test_function");
          g_assert(location.scope.empty());
          g_assert(location.kind == "function");
        }
        found = true;
        break;
      }
    }
    g_assert(found == true);
  }

  // Grep tests
  {
    auto result = Grep::get_result(tests_path, "ctags_grep_test_function", true, false);
    g_assert(result.first == tests_path.parent_path());
    bool found = false;
    std::string line;
    while(std::getline(*result.second, line)) {
      if(line.find("ctags_grep_test_function") != std::string::npos) {
        {
          auto location = Grep::get_location(line, true, true);
          g_assert(location.markup == "tests/ctags_grep_test.cc:7:void <b>ctags_grep_test_function</b>() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.offset, ==, 5);
          {
            auto location2 = Grep::get_location(location.markup, false, true);
            g_assert(location2.markup == "tests/ctags_grep_test.cc:7:void <b>ctags_grep_test_function</b>() {");
            g_assert(result.first / location2.file_path == tests_path / "ctags_grep_test.cc");
            g_assert_cmpint(location2.line, ==, 6);
            g_assert_cmpint(location2.offset, ==, 5);
          }
        }
        {
          auto location = Grep::get_location(line, true, false);
          g_assert(location.markup == "tests/ctags_grep_test.cc:7:void <b>ctags_grep_test_function</b>() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.offset, ==, 0);
          {
            auto location2 = Grep::get_location(location.markup, false, false);
            g_assert(location2.markup == "tests/ctags_grep_test.cc:7:void <b>ctags_grep_test_function</b>() {");
            g_assert(result.first / location2.file_path == tests_path / "ctags_grep_test.cc");
            g_assert_cmpint(location2.line, ==, 6);
            g_assert_cmpint(location2.offset, ==, 0);
          }
        }
        {
          auto location = Grep::get_location(line, true, true, (boost::filesystem::path("tests") / "ctags_grep_test.cc").string());
          g_assert(location.markup == "tests/ctags_grep_test.cc:7:void <b>ctags_grep_test_function</b>() {");
          g_assert(result.first / location.file_path == tests_path / "ctags_grep_test.cc");
          g_assert(location);
          g_assert_cmpint(location.line, ==, 6);
          g_assert_cmpint(location.offset, ==, 5);
        }
        {
          auto location = Grep::get_location(line, true, true, "CMakeLists.txt");
          g_assert(location.markup == "tests/ctags_grep_test.cc:7:void <b>ctags_grep_test_function</b>() {");
          g_assert(location.file_path.empty());
          g_assert(!location);
        }
        found = true;
        break;
      }
      else
        g_assert(false);
    }
    g_assert(found == true);
  }
  {
    auto pattern = std::string("C") + "tags_grep_test_function";
    {
      auto result = Grep::get_result(tests_path, pattern, true, false);
      g_assert(result.first == tests_path.parent_path());
      bool found = false;
      std::string line;
      while(std::getline(*result.second, line)) {
        if(line.find("ctags_grep_test_function") != std::string::npos)
          found = true;
      }
      g_assert(found == false);
    }
    {
      auto result = Grep::get_result(tests_path, pattern, false, false);
      g_assert(result.first == tests_path.parent_path());
      bool found = false;
      std::string line;
      while(std::getline(*result.second, line)) {
        if(line.find("ctags_grep_test_function") != std::string::npos)
          found = true;
      }
      g_assert(found == true);
    }
  }
}
