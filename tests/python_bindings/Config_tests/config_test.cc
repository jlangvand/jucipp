#include "test_suite.h"
#include <iostream>

int main() {
  const auto suite_name = "Config_tests";
  const auto doTest = [&](const std::string &test, const std::function<void(Config & config)> &assertions) {
    auto &config = Config::get();
    suite test_suite(suite_name);
    try {
      auto module = py::module::import("config_test");
      module.attr(test.c_str())();
      assertions(config);
      test_suite.has_assertion = true;
    }
    catch(const py::error_already_set &error) {
      std::cout << error.what();
    }
  };

  doTest("menu", [](Config &config) {
    g_assert_cmpstr(config.menu.keys.at("key").c_str(), ==, "value");
  });

  doTest("theme", [](Config &config) {
    g_assert_cmpstr(config.theme.name.c_str(), ==, "Star Wars");
    g_assert_cmpstr(config.theme.variant.c_str(), ==, "Instrumental");
    g_assert_cmpstr(config.theme.font.c_str(), ==, "Imperial");
  });

  doTest("terminal", [](Config &config) {
    g_assert_cmpstr(config.terminal.font.c_str(), ==, "Comic Sans");
    g_assert_cmpuint(config.terminal.history_size, ==, 3);
  });

  doTest("project", [](Config &config) {
    g_assert_cmpstr(config.project.default_build_path.c_str(), ==, "/build");
    g_assert_cmpstr(config.project.debug_build_path.c_str(), ==, "/debug");
    g_assert_cmpstr(config.project.meson.command.c_str(), ==, "meson");
    g_assert_cmpstr(config.project.meson.compile_command.c_str(), ==, "meson --build");
    g_assert_cmpstr(config.project.cmake.command.c_str(), ==, "cmake");
    g_assert_cmpstr(config.project.cmake.compile_command.c_str(), ==, "cmake --build");
    g_assert_true(config.project.save_on_compile_or_run);
    g_assert_false(config.project.clear_terminal_on_compile);
    g_assert_cmpstr(config.project.ctags_command.c_str(), ==, "ctags");
    g_assert_cmpstr(config.project.python_command.c_str(), ==, "python");
  });

  doTest("source", [](Config &config) {
    g_assert_cmpstr(config.source.style.c_str(), ==, "Classical");
    g_assert_cmpstr(config.source.font.c_str(), ==, "Monospaced");
    g_assert_cmpstr(config.source.spellcheck_language.c_str(), ==, "Klingon");
    g_assert_false(config.source.cleanup_whitespace_characters);
    g_assert_cmpstr(config.source.show_whitespace_characters.c_str(), ==, "no");
    g_assert_false(config.source.format_style_on_save);
    g_assert_false(config.source.format_style_on_save_if_style_file_found);
    g_assert_false(config.source.smart_inserts);
    g_assert_false(config.source.show_map);
    g_assert_cmpstr(config.source.map_font_size.c_str(), ==, "10px");
    g_assert_false(config.source.show_git_diff);
    g_assert_false(config.source.show_background_pattern);
    g_assert_false(config.source.show_right_margin);
    g_assert_cmpuint(config.source.right_margin_position, ==, 10);
    g_assert_false(config.source.auto_tab_char_and_size);
    g_assert_cmpint(config.source.default_tab_char, ==, 'c');
    g_assert_cmpuint(config.source.default_tab_size, ==, 1);
    g_assert_false(config.source.tab_indents_line);
    g_assert_false(config.source.wrap_lines);
    g_assert_false(config.source.highlight_current_line);
    g_assert_false(config.source.show_line_numbers);
    g_assert_false(config.source.enable_multiple_cursors);
    g_assert_false(config.source.auto_reload_changed_files);
    g_assert_cmpstr(config.source.clang_format_style.c_str(), ==, "CFS");
    g_assert_cmpuint(config.source.clang_usages_threads, ==, 1);
    g_assert_cmpuint(config.source.documentation_searches.size(), ==, 1);
    auto ds = config.source.documentation_searches.at("cpp");
    g_assert_cmpstr(ds.separator.c_str(), ==, "::");
    g_assert_cmpint(ds.queries.size(), ==, 1);
    g_assert_cmpstr(ds.queries.at("key").c_str(), ==, "value");
  });

  doTest("log", [](Config &config) {
    g_assert_true(config.log.libclang);
    g_assert_false(config.log.language_server);
  });

  doTest("cfg", [](Config &config) {
    g_assert_cmpstr(config.home_juci_path.string().c_str(), ==, "/away");
    g_assert_cmpstr(config.home_path.string().c_str(), ==, "/home");
  });
}
