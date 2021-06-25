#include "config.hpp"
#include "files.hpp"
#include "filesystem.hpp"
#include "terminal.hpp"
#include <algorithm>
#include <exception>
#include <iostream>

Config::Config() {
  home_path = filesystem::get_home_path();
  if(home_path.empty())
    throw std::runtime_error("Could not find home path");
  home_juci_path = home_path / ".juci";
}

void Config::load() {
  auto config_dir = home_juci_path / "config";
  auto config_json = config_dir / "config.json";
  try {
    boost::filesystem::create_directories(config_dir);

    if(!boost::filesystem::exists(config_json))
      filesystem::write(config_json, default_config_file);

    auto juci_style_path = home_juci_path / "styles";
    boost::filesystem::create_directories(juci_style_path);

    juci_style_path /= "juci-light.xml";
    if(!boost::filesystem::exists(juci_style_path))
      filesystem::write(juci_style_path, juci_light_style);
    juci_style_path = juci_style_path.parent_path();
    juci_style_path /= "juci-dark.xml";
    if(!boost::filesystem::exists(juci_style_path))
      filesystem::write(juci_style_path, juci_dark_style);
    juci_style_path = juci_style_path.parent_path();
    juci_style_path /= "juci-dark-blue.xml";
    if(!boost::filesystem::exists(juci_style_path))
      filesystem::write(juci_style_path, juci_dark_blue_style);

    JSON cfg(config_json);
    update(cfg);
    read(cfg);
  }
  catch(const std::exception &e) {
    dispatcher.post([config_json = std::move(config_json), e_what = std::string(e.what())] {
      ::Terminal::get().print("\e[31mError\e[m: could not parse " + filesystem::get_short_path(config_json).string() + ": " + e_what + "\n", true);
    });
    JSON default_cfg(default_config_file);
    read(default_cfg);
  }
}

void Config::update(JSON &cfg) {
  auto version = cfg.string("version");
  if(version == JUCI_VERSION)
    return;
  JSON default_cfg(default_config_file);
  make_version_dependent_corrections(cfg, default_cfg, version);
  cfg.set("version", JUCI_VERSION);

  add_missing_nodes(cfg, default_cfg);
  remove_deprecated_nodes(cfg, default_cfg);

  cfg.to_file(home_juci_path / "config" / "config.json", 2);

  auto style_path = home_juci_path / "styles";
  filesystem::write(style_path / "juci-light.xml", juci_light_style);
  filesystem::write(style_path / "juci-dark.xml", juci_dark_style);
  filesystem::write(style_path / "juci-dark-blue.xml", juci_dark_blue_style);
}

void Config::make_version_dependent_corrections(JSON &cfg, const JSON &default_cfg, const std::string &version) {
  try {
    if(version <= "1.2.4") {
      auto keybindings = cfg.object("keybindings");
      auto print = keybindings.string_optional("print");
      if(print && *print == "<primary>p") {
        keybindings.set("print", "");
        dispatcher.post([] {
          ::Terminal::get().print("Preference change: keybindings.print set to \"\"\n");
        });
      }
    }
  }
  catch(const std::exception &e) {
    std::cerr << "Error correcting preferences: " << e.what() << std::endl;
  }
}

void Config::add_missing_nodes(JSON &cfg, const JSON &default_cfg) {
  for(auto &default_cfg_child : default_cfg.children_or_empty()) {
    try {
      auto cfg_child = cfg.child(default_cfg_child.first);
      add_missing_nodes(cfg_child, default_cfg_child.second);
    }
    catch(...) {
      cfg.set(default_cfg_child.first, default_cfg_child.second);
    }
  }
}

void Config::remove_deprecated_nodes(JSON &cfg, const JSON &default_cfg) {
  auto children = cfg.children_or_empty();
  for(size_t i = 0; i < children.size();) {
    try {
      auto default_cfg_child = default_cfg.child(children[i].first);
      remove_deprecated_nodes(children[i].second, default_cfg_child);
      ++i;
    }
    catch(...) {
      cfg.remove(children[i].first);
      children = cfg.children_or_empty();
    }
  }
}

void Config::read(const JSON &cfg) {
  for(auto &keybinding : cfg.children("keybindings"))
    menu.keys[keybinding.first] = keybinding.second.string();

  auto source_json = cfg.object("source");
  source.style = source_json.string("style");
  source.font = source_json.string("font");
  source.cleanup_whitespace_characters = source_json.boolean("cleanup_whitespace_characters", JSON::ParseOptions::accept_string);
  source.show_whitespace_characters = source_json.string("show_whitespace_characters");
  source.format_style_on_save = source_json.boolean("format_style_on_save", JSON::ParseOptions::accept_string);
  source.format_style_on_save_if_style_file_found = source_json.boolean("format_style_on_save_if_style_file_found", JSON::ParseOptions::accept_string);
  source.smart_brackets = source_json.boolean("smart_brackets", JSON::ParseOptions::accept_string);
  source.smart_inserts = source_json.boolean("smart_inserts", JSON::ParseOptions::accept_string);
  if(source.smart_inserts)
    source.smart_brackets = true;
  source.show_map = source_json.boolean("show_map", JSON::ParseOptions::accept_string);
  source.map_font_size = source_json.integer("map_font_size", JSON::ParseOptions::accept_string);
  source.show_git_diff = source_json.boolean("show_git_diff", JSON::ParseOptions::accept_string);
  source.show_background_pattern = source_json.boolean("show_background_pattern", JSON::ParseOptions::accept_string);
  source.show_right_margin = source_json.boolean("show_right_margin", JSON::ParseOptions::accept_string);
  source.right_margin_position = source_json.integer("right_margin_position", JSON::ParseOptions::accept_string);
  source.spellcheck_language = source_json.string("spellcheck_language");
  auto default_tab_char_str = source_json.string("default_tab_char");
  if(default_tab_char_str.size() == 1)
    source.default_tab_char = default_tab_char_str[0];
  else
    source.default_tab_char = ' ';
  source.default_tab_size = source_json.integer("default_tab_size", JSON::ParseOptions::accept_string);
  source.auto_tab_char_and_size = source_json.boolean("auto_tab_char_and_size", JSON::ParseOptions::accept_string);
  source.tab_indents_line = source_json.boolean("tab_indents_line", JSON::ParseOptions::accept_string);
  source.word_wrap = source_json.string("word_wrap");
  source.highlight_current_line = source_json.boolean("highlight_current_line", JSON::ParseOptions::accept_string);
  source.show_line_numbers = source_json.boolean("show_line_numbers", JSON::ParseOptions::accept_string);
  source.enable_multiple_cursors = source_json.boolean("enable_multiple_cursors", JSON::ParseOptions::accept_string);
  source.auto_reload_changed_files = source_json.boolean("auto_reload_changed_files", JSON::ParseOptions::accept_string);
  source.search_for_selection = source_json.boolean("search_for_selection", JSON::ParseOptions::accept_string);
  source.clang_format_style = source_json.string("clang_format_style");
  source.clang_usages_threads = static_cast<unsigned>(source_json.integer("clang_usages_threads", JSON::ParseOptions::accept_string));
  source.clang_tidy_enable = source_json.boolean("clang_tidy_enable", JSON::ParseOptions::accept_string);
  source.clang_tidy_checks = source_json.string("clang_tidy_checks");
  source.clang_detailed_preprocessing_record = source_json.boolean("clang_detailed_preprocessing_record", JSON::ParseOptions::accept_string);
  source.debug_place_cursor_at_stop = source_json.boolean("debug_place_cursor_at_stop", JSON::ParseOptions::accept_string);

  for(auto &documentation_searches : cfg.children("documentation_searches")) {
    auto &documentation_search = source.documentation_searches[documentation_searches.first];
    documentation_search.separator = documentation_searches.second.string("separator");
    for(auto &i : documentation_searches.second.children("queries"))
      documentation_search.queries[i.first] = i.second.string();
  }

  auto theme_json = cfg.object("gtk_theme");
  theme.name = theme_json.string("name");
  theme.variant = theme_json.string("variant");
  theme.font = theme_json.string("font");

  auto project_json = cfg.object("project");
  project.default_build_path = project_json.string("default_build_path");
  project.debug_build_path = project_json.string("debug_build_path");
  auto cmake_json = project_json.object("cmake");
  project.cmake.command = cmake_json.string("command");
  project.cmake.compile_command = cmake_json.string("compile_command");
  auto meson_json = project_json.object("meson");
  project.meson.command = meson_json.string("command");
  project.meson.compile_command = meson_json.string("compile_command");
  project.default_build_management_system = project_json.string("default_build_management_system");
  project.save_on_compile_or_run = project_json.boolean("save_on_compile_or_run", JSON::ParseOptions::accept_string);
  project.ctags_command = project_json.string("ctags_command");
  project.grep_command = project_json.string("grep_command");
  project.cargo_command = project_json.string("cargo_command");
  project.python_command = project_json.string("python_command");
  project.markdown_command = project_json.string("markdown_command");

  auto terminal_json = cfg.object("terminal");
  terminal.history_size = terminal_json.integer("history_size", JSON::ParseOptions::accept_string);
  terminal.font = terminal_json.string("font");
  terminal.clear_on_compile = terminal_json.boolean("clear_on_compile", JSON::ParseOptions::accept_string);
  terminal.clear_on_run_command = terminal_json.boolean("clear_on_run_command", JSON::ParseOptions::accept_string);
  terminal.hide_entry_on_run_command = terminal_json.boolean("hide_entry_on_run_command", JSON::ParseOptions::accept_string);

  auto log_json = cfg.object("log");
  log.libclang = log_json.boolean("libclang", JSON::ParseOptions::accept_string);
  log.language_server = log_json.boolean("language_server", JSON::ParseOptions::accept_string);
}
