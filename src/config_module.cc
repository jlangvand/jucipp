#include "config.h"
#include <pybind11/stl.h>
#include "python_type_casters.h"

void Config::init_module(py::module &api) {
  py::class_<Config, std::unique_ptr<Config, py::nodelete>> config(api, "Config");

  py::class_<Config::Menu>(config, "Menu")
      .def(py::init())
      .def_readwrite("keys", &Config::Menu::keys)

      ;
  py::class_<Config::Theme>(config, "Theme")
      .def(py::init())
      .def_readwrite("name", &Config::Theme::name)
      .def_readwrite("variant", &Config::Theme::variant)
      .def_readwrite("font", &Config::Theme::font)

      ;
  py::class_<Config::Terminal>(config, "Terminal")
      .def(py::init())
      .def_readwrite("history_size", &Config::Terminal::history_size)
      .def_readwrite("font", &Config::Terminal::font)

      ;
  py::class_<Config::Project> project(config, "Project");
  py::class_<Config::Project::CMake>(project, "CMake")
      .def(py::init())
      .def_readwrite("command", &Config::Project::CMake::command)
      .def_readwrite("compile_command", &Config::Project::CMake::compile_command)

      ;
  py::class_<Config::Project::Meson>(project, "Meson")
      .def(py::init())
      .def_readwrite("command", &Config::Project::Meson::command)
      .def_readwrite("compile_command", &Config::Project::Meson::compile_command)

      ;
  project
      .def(py::init())
      .def_readwrite("default_build_path", &Config::Project::default_build_path)
      .def_readwrite("debug_build_path", &Config::Project::debug_build_path)
      .def_readwrite("cmake", &Config::Project::cmake)
      .def_readwrite("meson", &Config::Project::meson)
      .def_readwrite("save_on_compile_or_run", &Config::Project::save_on_compile_or_run)
      .def_readwrite("clear_terminal_on_compile", &Config::Project::clear_terminal_on_compile)
      .def_readwrite("ctags_command", &Config::Project::ctags_command)
      .def_readwrite("python_command", &Config::Project::python_command)

      ;
  py::class_<Config::Source> source(config, "Source");
  py::class_<Config::Source::DocumentationSearch>(source, "DocumentationSearch")
      .def(py::init())
      .def_readwrite("separator", &Config::Source::DocumentationSearch::separator)
      .def_readwrite("queries", &Config::Source::DocumentationSearch::queries)

      ;
  source
      .def(py::init())
      .def_readwrite("style", &Config::Source::style)
      .def_readwrite("font", &Config::Source::font)
      .def_readwrite("spellcheck_language", &Config::Source::spellcheck_language)
      .def_readwrite("cleanup_whitespace_characters", &Config::Source::cleanup_whitespace_characters)
      .def_readwrite("show_whitespace_characters", &Config::Source::show_whitespace_characters)
      .def_readwrite("format_style_on_save", &Config::Source::format_style_on_save)
      .def_readwrite("format_style_on_save_if_style_file_found", &Config::Source::format_style_on_save_if_style_file_found)
      .def_readwrite("smart_inserts", &Config::Source::smart_inserts)
      .def_readwrite("show_map", &Config::Source::show_map)
      .def_readwrite("map_font_size", &Config::Source::map_font_size)
      .def_readwrite("show_git_diff", &Config::Source::show_git_diff)
      .def_readwrite("show_background_pattern", &Config::Source::show_background_pattern)
      .def_readwrite("show_right_margin", &Config::Source::show_right_margin)
      .def_readwrite("right_margin_position", &Config::Source::right_margin_position)
      .def_readwrite("auto_tab_char_and_size", &Config::Source::auto_tab_char_and_size)
      .def_readwrite("default_tab_char", &Config::Source::default_tab_char)
      .def_readwrite("default_tab_size", &Config::Source::default_tab_size)
      .def_readwrite("tab_indents_line", &Config::Source::tab_indents_line)
      .def_readwrite("wrap_lines", &Config::Source::wrap_lines)
      .def_readwrite("highlight_current_line", &Config::Source::highlight_current_line)
      .def_readwrite("show_line_numbers", &Config::Source::show_line_numbers)
      .def_readwrite("enable_multiple_cursors", &Config::Source::enable_multiple_cursors)
      .def_readwrite("auto_reload_changed_files", &Config::Source::auto_reload_changed_files)
      .def_readwrite("clang_format_style", &Config::Source::clang_format_style)
      .def_readwrite("clang_usages_threads", &Config::Source::clang_usages_threads)
      .def_readwrite("documentation_searches", &Config::Source::documentation_searches)

      ;

  py::class_<Config::Log>(config, "Log")
      .def(py::init())
      .def_readwrite("libclang", &Config::Log::libclang)
      .def_readwrite("language_server", &Config::Log::language_server)

      ;
  py::class_<Config::Plugins>(config, "Plugins")
      .def(py::init())
      .def_readwrite("enabled", &Config::Plugins::enabled)
      .def_readwrite("path", &Config::Plugins::path)

      ;
  config
      .def(py::init([]() { return &(Config::get()); }))
      .def("load", &Config::load)
      .def_readonly("version", &Config::version)
      .def_readwrite("menu", &Config::menu)
      .def_readwrite("theme", &Config::theme)
      .def_readwrite("terminal", &Config::terminal)
      .def_readwrite("project", &Config::project)
      .def_readwrite("source", &Config::source)
      .def_readwrite("log", &Config::log)
      .def_readwrite("plugins", &Config::plugins)
      .def_readwrite("home_path", &Config::home_path)
      .def_readwrite("home_juci_path", &Config::home_juci_path)

      ;
}
