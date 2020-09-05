#pragma once
#include "dispatcher.hpp"
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Config {
public:
  class Menu {
  public:
    std::unordered_map<std::string, std::string> keys;
  };

  class Theme {
  public:
    std::string name;
    std::string variant;
    std::string font;
  };

  class Terminal {
  public:
    int history_size;
    std::string font;
    bool clear_on_compile;
    bool clear_on_run_command;
  };

  class Project {
  public:
    class CMake {
    public:
      std::string command;
      std::string compile_command;
    };
    class Meson {
    public:
      std::string command;
      std::string compile_command;
    };

    std::string default_build_path;
    std::string debug_build_path;
    CMake cmake;
    Meson meson;
    std::string default_build_management_system;
    bool save_on_compile_or_run;
    std::string ctags_command;
    std::string grep_command;
    std::string cargo_command;
    std::string python_command;
    std::string markdown_command;
  };

  class Source {
  public:
    class DocumentationSearch {
    public:
      std::string separator;
      std::unordered_map<std::string, std::string> queries;
    };

    std::string style;
    std::string font;
    std::string spellcheck_language;

    bool cleanup_whitespace_characters;
    std::string show_whitespace_characters;

    bool format_style_on_save;
    bool format_style_on_save_if_style_file_found;

    bool smart_brackets;
    bool smart_inserts;

    bool show_map;
    unsigned map_font_size;
    bool show_git_diff;
    bool show_background_pattern;
    bool show_right_margin;
    unsigned right_margin_position;

    bool auto_tab_char_and_size;
    char default_tab_char;
    unsigned default_tab_size;
    bool tab_indents_line;
    std::string word_wrap;
    bool highlight_current_line;
    bool show_line_numbers;
    bool enable_multiple_cursors;
    bool auto_reload_changed_files;
    bool search_for_selection;

    std::string clang_format_style;
    unsigned clang_usages_threads;

    bool enable_clang_tidy;
    std::string clang_tidy_checks;

    bool enable_clang_detailed_preprocessing_record = false;

    bool debug_place_cursor_at_stop;

    std::unordered_map<std::string, DocumentationSearch> documentation_searches;
  };

  class Log {
  public:
    bool libclang;
    bool language_server;
  };

private:
  Config();

public:
  static Config &get() {
    static Config singleton;
    return singleton;
  }

  void load();

  std::string version;
  Menu menu;
  Theme theme;
  Terminal terminal;
  Project project;
  Source source;
  Log log;

  boost::filesystem::path home_path;
  boost::filesystem::path home_juci_path;

private:
  /// Used to dispatch Terminal outputs after juCi++ GUI setup and configuration
  Dispatcher dispatcher;

  void update(boost::property_tree::ptree &cfg);
  void make_version_dependent_corrections(boost::property_tree::ptree &cfg, const boost::property_tree::ptree &default_cfg, const std::string &version);
  bool add_missing_nodes(boost::property_tree::ptree &cfg, const boost::property_tree::ptree &default_cfg, std::string parent_path = "");
  bool remove_deprecated_nodes(boost::property_tree::ptree &cfg, const boost::property_tree::ptree &default_cfg, std::string parent_path = "");
  void read(const boost::property_tree::ptree &cfg);
};
