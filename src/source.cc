#include "source.h"
#include "config.h"
#include "directories.h"
#include "filesystem.h"
#include "git.h"
#include "info.h"
#include "menu.h"
#include "selection_dialog.h"
#include "terminal.h"
#include "utility.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/spirit/home/qi/char.hpp>
#include <boost/spirit/home/qi/operator.hpp>
#include <boost/spirit/home/qi/string.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <regex>
#include <set>

Glib::RefPtr<Gsv::LanguageManager> Source::LanguageManager::get_default() {
  static auto instance = Gsv::LanguageManager::create();
  return instance;
}
Glib::RefPtr<Gsv::StyleSchemeManager> Source::StyleSchemeManager::get_default() {
  static auto instance = Gsv::StyleSchemeManager::create();
  static bool first = true;
  if(first) {
    instance->prepend_search_path((Config::get().home_juci_path / "styles").string());
    first = false;
  }
  return instance;
}

Glib::RefPtr<Gsv::Language> Source::guess_language(const boost::filesystem::path &file_path) {
  auto language_manager = LanguageManager::get_default();
  bool result_uncertain = false;
  auto content_type = Gio::content_type_guess(file_path.string(), nullptr, 0, result_uncertain);
  if(result_uncertain)
    content_type.clear();
  auto language = language_manager->guess_language(file_path.string(), content_type);
  if(!language) {
    auto filename = file_path.filename().string();
    if(filename == "CMakeLists.txt")
      language = language_manager->get_language("cmake");
    else if(filename == "Makefile")
      language = language_manager->get_language("makefile");
    else if(file_path.extension() == ".tcc")
      language = language_manager->get_language("cpphdr");
    else if(file_path.extension() == ".ts" || file_path.extension() == ".jsx" || file_path.extension() == ".flow")
      language = language_manager->get_language("js");
    else if(!file_path.has_extension()) {
      for(auto &part : file_path) {
        if(part == "include") {
          language = language_manager->get_language("cpphdr");
          break;
        }
      }
    }
  }
  else if(language->get_id() == "cuda") {
    if(file_path.extension() == ".cuh")
      language = language_manager->get_language("cpphdr");
    else
      language = language_manager->get_language("cpp");
  }
  else if(language->get_id() == "opencl") {
    language = language_manager->get_language("cpp");
  }
  return language;
}

Source::FixIt::FixIt(std::string source_, std::pair<Offset, Offset> offsets_) : source(std::move(source_)), offsets(std::move(offsets_)) {
  if(this->source.size() == 0)
    type = Type::ERASE;
  else {
    if(this->offsets.first == this->offsets.second)
      type = Type::INSERT;
    else
      type = Type::REPLACE;
  }
}

std::string Source::FixIt::string(const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
  auto iter = buffer->get_iter_at_line_index(offsets.first.line, offsets.first.index);
  unsigned first_line_offset = iter.get_line_offset() + 1;
  iter = buffer->get_iter_at_line_index(offsets.second.line, offsets.second.index);
  unsigned second_line_offset = iter.get_line_offset() + 1;

  std::string text;
  if(type == Type::INSERT) {
    text += "Insert " + source + " at ";
    text += std::to_string(offsets.first.line + 1) + ":" + std::to_string(first_line_offset);
  }
  else if(type == Type::REPLACE) {
    text += "Replace ";
    text += std::to_string(offsets.first.line + 1) + ":" + std::to_string(first_line_offset) + " - ";
    text += std::to_string(offsets.second.line + 1) + ":" + std::to_string(second_line_offset);
    text += " with " + source;
  }
  else {
    text += "Erase ";
    text += std::to_string(offsets.first.line + 1) + ":" + std::to_string(first_line_offset) + " - ";
    text += std::to_string(offsets.second.line + 1) + ":" + std::to_string(second_line_offset);
  }

  return text;
}

//////////////
//// View ////
//////////////
std::set<Source::View *> Source::View::non_deleted_views;
std::set<Source::View *> Source::View::views;

Source::View::View(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language, bool is_generic_view) : BaseView(file_path, language), SpellCheckView(file_path, language), DiffView(file_path, language) {
  non_deleted_views.emplace(this);
  views.emplace(this);

  similar_symbol_tag = get_buffer()->create_tag();
  similar_symbol_tag->property_weight() = Pango::WEIGHT_ULTRAHEAVY;
  clickable_tag = get_buffer()->create_tag();
  clickable_tag->property_underline() = Pango::Underline::UNDERLINE_SINGLE;
  clickable_tag->property_underline_set() = true;

  get_buffer()->create_tag("def:warning");
  get_buffer()->create_tag("def:warning_underline");
  get_buffer()->create_tag("def:error");
  get_buffer()->create_tag("def:error_underline");

  auto mark_attr_debug_breakpoint = Gsv::MarkAttributes::create();
  Gdk::RGBA rgba;
  rgba.set_red(1.0);
  rgba.set_green(0.5);
  rgba.set_blue(0.5);
  rgba.set_alpha(0.3);
  mark_attr_debug_breakpoint->set_background(rgba);
  set_mark_attributes("debug_breakpoint", mark_attr_debug_breakpoint, 100);
  auto mark_attr_debug_stop = Gsv::MarkAttributes::create();
  rgba.set_red(0.5);
  rgba.set_green(0.5);
  rgba.set_blue(1.0);
  mark_attr_debug_stop->set_background(rgba);
  set_mark_attributes("debug_stop", mark_attr_debug_stop, 101);
  auto mark_attr_debug_breakpoint_and_stop = Gsv::MarkAttributes::create();
  rgba.set_red(0.75);
  rgba.set_green(0.5);
  rgba.set_blue(0.75);
  mark_attr_debug_breakpoint_and_stop->set_background(rgba);
  set_mark_attributes("debug_breakpoint_and_stop", mark_attr_debug_breakpoint_and_stop, 102);

  link_tag = get_buffer()->create_tag("link");

  if(language) {
    auto language_id = language->get_id();
    if(language_id == "chdr" || language_id == "cpphdr" || language_id == "c" || language_id == "cpp") {
      is_cpp = true;
      use_fixed_continuation_indenting = false;
      // TODO 2019: check if clang-format has improved...
      // boost::filesystem::path clang_format_file;
      // auto search_path=file_path.parent_path();
      // boost::system::error_code ec;
      // while(true) {
      //   clang_format_file=search_path/".clang-format";
      //   if(boost::filesystem::exists(clang_format_file, ec))
      //     break;
      //   clang_format_file=search_path/"_clang-format";
      //   if(boost::filesystem::exists(clang_format_file, ec))
      //     break;
      //   clang_format_file.clear();

      //   if(search_path==search_path.root_directory())
      //     break;
      //   search_path=search_path.parent_path();
      // }
      // if(!clang_format_file.empty()) {
      //   auto lines=filesystem::read_lines(clang_format_file);
      //   for(auto &line: lines) {
      //     std::cout << "1" << std::endl;
      //     if(!line.empty() && line.compare(0, 23, "ContinuationIndentWidth")==0) {
      //       std::cout << "2" << std::endl;
      //       use_continuation_indenting=true;
      //       break;
      //     }
      //   }
      // }
    }
  }

  setup_signals();
  setup_format_style(is_generic_view);

  std::string comment_characters;
  if(is_bracket_language)
    comment_characters = "//";
  else if(language) {
    auto language_id = language->get_id();
    if(language_id == "cmake" || language_id == "makefile" || language_id == "python" ||
       language_id == "python3" || language_id == "sh" || language_id == "perl" ||
       language_id == "ruby" || language_id == "r" || language_id == "asm" ||
       language_id == "automake" || language_id == "yaml")
      comment_characters = "#";
    else if(language_id == "latex" || language_id == "matlab" || language_id == "octave" || language_id == "bibtex")
      comment_characters = "%";
    else if(language_id == "fortran")
      comment_characters = "!";
    else if(language_id == "pascal")
      comment_characters = "//";
    else if(language_id == "lua")
      comment_characters = "--";
  }
  if(!comment_characters.empty()) {
    toggle_comments = [this, comment_characters = std::move(comment_characters)] {
      std::vector<int> lines;
      Gtk::TextIter selection_start, selection_end;
      get_buffer()->get_selection_bounds(selection_start, selection_end);
      auto line_start = selection_start.get_line();
      auto line_end = selection_end.get_line();
      if(line_start != line_end && selection_end.starts_line())
        --line_end;
      bool lines_commented = true;
      bool extra_spaces = true;
      int min_indentation = -1;
      for(auto line = line_start; line <= line_end; ++line) {
        auto iter = get_buffer()->get_iter_at_line(line);
        bool line_added = false;
        bool line_commented = false;
        bool extra_space = false;
        int indentation = 0;
        for(;;) {
          if(iter.ends_line())
            break;
          else if(*iter == ' ' || *iter == '\t') {
            ++indentation;
            iter.forward_char();
            continue;
          }
          else {
            lines.emplace_back(line);
            line_added = true;
            for(size_t c = 0; c < comment_characters.size(); ++c) {
              if(iter.ends_line()) {
                break;
              }
              else if(*iter == static_cast<unsigned int>(comment_characters[c])) {
                if(c < comment_characters.size() - 1) {
                  iter.forward_char();
                  continue;
                }
                else {
                  line_commented = true;
                  if(!iter.ends_line()) {
                    iter.forward_char();
                    if(*iter == ' ')
                      extra_space = true;
                  }
                  break;
                }
              }
              else
                break;
            }
            break;
          }
        }
        if(line_added) {
          lines_commented &= line_commented;
          extra_spaces &= extra_space;
          if(min_indentation == -1 || indentation < min_indentation)
            min_indentation = indentation;
        }
      }
      if(lines.size()) {
        auto comment_characters_and_space = comment_characters + ' ';
        get_buffer()->begin_user_action();
        for(auto &line : lines) {
          auto iter = get_buffer()->get_iter_at_line(line);
          iter.forward_chars(min_indentation);
          if(lines_commented) {
            auto end_iter = iter;
            end_iter.forward_chars(comment_characters.size() + static_cast<int>(extra_spaces));
            while(*iter == ' ' || *iter == '\t') {
              iter.forward_char();
              end_iter.forward_char();
            }
            get_buffer()->erase(iter, end_iter);
          }
          else
            get_buffer()->insert(iter, comment_characters_and_space);
        }
        get_buffer()->end_user_action();
      }
    };
  }
}

Gsv::DrawSpacesFlags Source::View::parse_show_whitespace_characters(const std::string &text) {
  namespace qi = boost::spirit::qi;

  qi::symbols<char, Gsv::DrawSpacesFlags> options;
  options.add("space", Gsv::DRAW_SPACES_SPACE)("tab", Gsv::DRAW_SPACES_TAB)("newline", Gsv::DRAW_SPACES_NEWLINE)("nbsp", Gsv::DRAW_SPACES_NBSP)
      ("leading", Gsv::DRAW_SPACES_LEADING)("text", Gsv::DRAW_SPACES_TEXT)("trailing", Gsv::DRAW_SPACES_TRAILING)("all", Gsv::DRAW_SPACES_ALL);

  std::set<Gsv::DrawSpacesFlags> out;

  // parse comma-separated list of options
  qi::phrase_parse(text.begin(), text.end(), options % ',', qi::space, out);

  return out.count(Gsv::DRAW_SPACES_ALL) > 0 ? Gsv::DRAW_SPACES_ALL : static_cast<Gsv::DrawSpacesFlags>(std::accumulate(out.begin(), out.end(), 0));
}

bool Source::View::save() {
  if(file_path.empty() || !get_buffer()->get_modified())
    return false;
  if(Config::get().source.cleanup_whitespace_characters)
    cleanup_whitespace_characters();

  if(format_style && file_path.filename() != "package.json") {
    if(Config::get().source.format_style_on_save)
      format_style(true);
    else if(Config::get().source.format_style_on_save_if_style_file_found)
      format_style(false);
    hide_tooltips();
  }

  std::ofstream output(file_path.string(), std::ofstream::binary);
  if(output) {
    auto start_iter = get_buffer()->begin();
    auto end_iter = start_iter;
    bool end_reached = false;
    while(!end_reached) {
      for(size_t c = 0; c < 131072; c++) {
        if(!end_iter.forward_char()) {
          end_reached = true;
          break;
        }
      }
      output << get_buffer()->get_text(start_iter, end_iter).c_str();
      start_iter = end_iter;
    }
    output.close();
    boost::system::error_code ec;
    last_write_time = boost::filesystem::last_write_time(file_path, ec);
    if(ec)
      last_write_time = static_cast<std::time_t>(-1);
    // Remonitor file in case it did not exist before
    monitor_file();
    get_buffer()->set_modified(false);
    Directories::get().on_save_file(file_path);
    return true;
  }
  else {
    Terminal::get().print("Error: could not save file " + file_path.string() + '\n', true);
    return false;
  }
}

void Source::View::configure() {
  SpellCheckView::configure();
  DiffView::configure();

  if(Config::get().source.style.size() > 0) {
    auto scheme = StyleSchemeManager::get_default()->get_scheme(Config::get().source.style);
    if(scheme)
      get_source_buffer()->set_style_scheme(scheme);
  }

  set_draw_spaces(parse_show_whitespace_characters(Config::get().source.show_whitespace_characters));

  if(Config::get().source.wrap_lines)
    set_wrap_mode(Gtk::WrapMode::WRAP_CHAR);
  else
    set_wrap_mode(Gtk::WrapMode::WRAP_NONE);
  property_highlight_current_line() = Config::get().source.highlight_current_line;
  property_show_line_numbers() = Config::get().source.show_line_numbers;
  if(Config::get().source.font.size() > 0)
    override_font(Pango::FontDescription(Config::get().source.font));

#if GTKMM_MAJOR_VERSION > 3 || (GTKMM_MAJOR_VERSION == 3 && GTKMM_MINOR_VERSION >= 20)
  Gdk::Rectangle rectangle;
  get_iter_location(get_buffer()->begin(), rectangle);
  set_bottom_margin((rectangle.get_height() + get_pixels_above_lines() + get_pixels_below_lines()) * 10);
#endif

  if(Config::get().source.show_background_pattern)
    gtk_source_view_set_background_pattern(this->gobj(), GTK_SOURCE_BACKGROUND_PATTERN_TYPE_GRID);
  else
    gtk_source_view_set_background_pattern(this->gobj(), GTK_SOURCE_BACKGROUND_PATTERN_TYPE_NONE);
  property_show_right_margin() = Config::get().source.show_right_margin;
  property_right_margin_position() = Config::get().source.right_margin_position;

  //Create tags for diagnostic warnings and errors:
  auto scheme = get_source_buffer()->get_style_scheme();
  auto tag_table = get_buffer()->get_tag_table();
  auto style = scheme->get_style("def:warning");
  auto diagnostic_tag = get_buffer()->get_tag_table()->lookup("def:warning");
  auto diagnostic_tag_underline = get_buffer()->get_tag_table()->lookup("def:warning_underline");
  if(style && (style->property_foreground_set() || style->property_background_set())) {
    Glib::ustring warning_property;
    if(style->property_foreground_set()) {
      warning_property = style->property_foreground().get_value();
      diagnostic_tag->property_foreground() = warning_property;
    }
    else if(style->property_background_set())
      warning_property = style->property_background().get_value();

    diagnostic_tag_underline->property_underline() = Pango::Underline::UNDERLINE_ERROR;
    auto tag_class = G_OBJECT_GET_CLASS(diagnostic_tag_underline->gobj()); //For older GTK+ 3 versions:
    auto param_spec = g_object_class_find_property(tag_class, "underline-rgba");
    if(param_spec != nullptr) {
      diagnostic_tag_underline->set_property("underline-rgba", Gdk::RGBA(warning_property));
    }
  }
  style = scheme->get_style("def:error");
  diagnostic_tag = get_buffer()->get_tag_table()->lookup("def:error");
  diagnostic_tag_underline = get_buffer()->get_tag_table()->lookup("def:error_underline");
  if(style && (style->property_foreground_set() || style->property_background_set())) {
    Glib::ustring error_property;
    if(style->property_foreground_set()) {
      error_property = style->property_foreground().get_value();
      diagnostic_tag->property_foreground() = error_property;
    }
    else if(style->property_background_set())
      error_property = style->property_background().get_value();

    diagnostic_tag_underline->property_underline() = Pango::Underline::UNDERLINE_ERROR;
    diagnostic_tag_underline->set_property("underline-rgba", Gdk::RGBA(error_property));
  }
  //TODO: clear tag_class and param_spec?

  link_tag->property_foreground_rgba() = get_style_context()->get_color(Gtk::StateFlags::STATE_FLAG_LINK);
  link_tag->property_underline() = Pango::Underline::UNDERLINE_SINGLE;

  if(Config::get().menu.keys["source_show_completion"].empty()) {
    get_completion()->unblock_interactive();
    interactive_completion = true;
  }
  else {
    get_completion()->block_interactive();
    interactive_completion = false;
  }
}

void Source::View::setup_signals() {
  get_buffer()->signal_changed().connect([this]() {
    if(update_status_location)
      update_status_location(this);

    hide_tooltips();

    if(similar_symbol_tag_applied) {
      get_buffer()->remove_tag(similar_symbol_tag, get_buffer()->begin(), get_buffer()->end());
      similar_symbol_tag_applied = false;
    }
    if(clickable_tag_applied) {
      get_buffer()->remove_tag(clickable_tag, get_buffer()->begin(), get_buffer()->end());
      clickable_tag_applied = false;
    }
  });

  signal_realize().connect([this] {
    auto gutter = get_gutter(Gtk::TextWindowType::TEXT_WINDOW_LEFT);
    auto renderer = gutter->get_renderer_at_pos(15, 0);
    if(renderer) {
      renderer_activate_connection.disconnect();
      renderer_activate_connection = renderer->signal_activate().connect([this](const Gtk::TextIter &iter, const Gdk::Rectangle &, GdkEvent *) {
        if(toggle_breakpoint)
          toggle_breakpoint(iter.get_line());
      });
    }
  });

  type_tooltips.on_motion = [this] {
    delayed_tooltips_connection.disconnect();
  };
  diagnostic_tooltips.on_motion = [this] {
    delayed_tooltips_connection.disconnect();
  };

  signal_motion_notify_event().connect([this](GdkEventMotion *event) {
    if(on_motion_last_x != event->x || on_motion_last_y != event->y) {
      delayed_tooltips_connection.disconnect();
      if((event->state & GDK_BUTTON1_MASK) == 0) {
        gdouble x = event->x;
        gdouble y = event->y;
        delayed_tooltips_connection = Glib::signal_timeout().connect([this, x, y]() {
          type_tooltips.hide();
          diagnostic_tooltips.hide();
          Tooltips::init();
          Gdk::Rectangle rectangle(x, y, 1, 1);
          if(parsed) {
            show_type_tooltips(rectangle);
            show_diagnostic_tooltips(rectangle);
          }
          return false;
        }, 100);
      }

      if(clickable_tag_applied) {
        get_buffer()->remove_tag(clickable_tag, get_buffer()->begin(), get_buffer()->end());
        clickable_tag_applied = false;
      }
      if((event->state & primary_modifier_mask) && !(event->state & GDK_SHIFT_MASK) && !(event->state & GDK_BUTTON1_MASK)) {
        delayed_tag_clickable_connection.disconnect();
        delayed_tag_clickable_connection = Glib::signal_timeout().connect([this, x = event->x, y = event->y]() {
          int buffer_x, buffer_y;
          window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, x, y, buffer_x, buffer_y);
          Gtk::TextIter iter;
          get_iter_at_location(iter, buffer_x, buffer_y);
          apply_clickable_tag(iter);
          clickable_tag_applied = true;
          return false;
        }, 100);
      }

      auto last_mouse_pos = std::make_pair(on_motion_last_x, on_motion_last_y);
      auto mouse_pos = std::make_pair(event->x, event->y);
      type_tooltips.hide(last_mouse_pos, mouse_pos);
      diagnostic_tooltips.hide(last_mouse_pos, mouse_pos);
    }
    on_motion_last_x = event->x;
    on_motion_last_y = event->y;
    return false;
  });

  get_buffer()->signal_mark_set().connect([this](const Gtk::TextBuffer::iterator &iterator, const Glib::RefPtr<Gtk::TextBuffer::Mark> &mark) {
    if(get_buffer()->get_has_selection() && mark->get_name() == "selection_bound")
      delayed_tooltips_connection.disconnect();

    if(mark->get_name() == "insert") {
      hide_tooltips();

      delayed_tooltips_connection.disconnect();
      delayed_tooltips_connection = Glib::signal_timeout().connect([this]() {
        Tooltips::init();
        Gdk::Rectangle rectangle;
        get_iter_location(get_buffer()->get_insert()->get_iter(), rectangle);
        int location_window_x, location_window_y;
        buffer_to_window_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, rectangle.get_x(), rectangle.get_y(), location_window_x, location_window_y);
        rectangle.set_x(location_window_x - 2);
        rectangle.set_y(location_window_y);
        rectangle.set_width(5);
        if(parsed) {
          show_type_tooltips(rectangle);
          show_diagnostic_tooltips(rectangle);
        }
        return false;
      }, 500);

      delayed_tag_similar_symbols_connection.disconnect();
      delayed_tag_similar_symbols_connection = Glib::signal_timeout().connect([this] {
        apply_similar_symbol_tag();
        similar_symbol_tag_applied = true;
        return false;
      }, 100);

      if(SelectionDialog::get())
        SelectionDialog::get()->hide();
      if(CompletionDialog::get())
        CompletionDialog::get()->hide();

      if(update_status_location)
        update_status_location(this);
    }
  });

  signal_key_release_event().connect([this](GdkEventKey *event) {
    if((event->state & primary_modifier_mask) && clickable_tag_applied) {
      get_buffer()->remove_tag(clickable_tag, get_buffer()->begin(), get_buffer()->end());
      clickable_tag_applied = false;
    }
    return false;
  });

  signal_scroll_event().connect([this](GdkEventScroll *event) {
    hide_tooltips();
    hide_dialogs();
    return false;
  });

  signal_focus_out_event().connect([this](GdkEventFocus *event) {
    hide_tooltips();
    if(clickable_tag_applied) {
      get_buffer()->remove_tag(clickable_tag, get_buffer()->begin(), get_buffer()->end());
      clickable_tag_applied = false;
    }
    return false;
  });

  signal_leave_notify_event().connect([this](GdkEventCrossing *) {
    delayed_tooltips_connection.disconnect();
    return false;
  });
}

void Source::View::setup_format_style(bool is_generic_view) {
  static auto prettier = filesystem::find_executable("prettier");
  if(!prettier.empty() && language &&
     (language->get_id() == "js" || language->get_id() == "json" || language->get_id() == "css")) {
    if(is_generic_view) {
      goto_next_diagnostic = [this] {
        place_cursor_at_next_diagnostic();
      };
      get_buffer()->signal_changed().connect([this] {
        clear_diagnostic_tooltips();
        status_diagnostics = std::make_tuple<size_t, size_t, size_t>(0, 0, 0);
        if(update_status_diagnostics)
          update_status_diagnostics(this);
      });
    }
    format_style = [this, is_generic_view](bool continue_without_style_file) {
      auto command = prettier.string();
      if(!continue_without_style_file) {
        auto search_path = file_path.parent_path();
        while(true) {
          static std::vector<boost::filesystem::path> files = {".prettierrc", ".prettierrc.yaml", ".prettierrc.yml", ".prettierrc.json", ".prettierrc.toml", ".prettierrc.js", "prettier.config.js"};
          boost::system::error_code ec;
          bool found = false;
          for(auto &file : files) {
            if(boost::filesystem::exists(search_path / file, ec)) {
              found = true;
              break;
            }
          }
          if(found)
            break;
          auto package_json = search_path / "package.json";
          if(boost::filesystem::exists(package_json, ec)) {
            boost::property_tree::ptree pt;
            try {
              boost::property_tree::json_parser::read_json(package_json.string(), pt);
              auto child = pt.get_child("prettier");
              break;
            }
            catch(...) {
            }
          }

          if(search_path == search_path.root_directory())
            return;
          search_path = search_path.parent_path();
        }
      }

      command += " --stdin-filepath " + this->file_path.string() + " --print-width 120 --config-precedence prefer-file";

      if(get_buffer()->get_has_selection()) { // Cannot be used together with --cursor-offset
        Gtk::TextIter start, end;
        get_buffer()->get_selection_bounds(start, end);
        command += " --range-start " + std::to_string(start.get_offset());
        command += " --range-end " + std::to_string(end.get_offset());
      }
      else
        command += " --cursor-offset " + std::to_string(get_buffer()->get_insert()->get_iter().get_offset());

      size_t num_warnings = 0, num_errors = 0, num_fix_its = 0;
      if(is_generic_view)
        clear_diagnostic_tooltips();

      std::stringstream stdin_stream(get_buffer()->get_text()), stdout_stream, stderr_stream;
      auto exit_status = Terminal::get().process(stdin_stream, stdout_stream, command, this->file_path.parent_path(), &stderr_stream);
      if(exit_status == 0) {
        replace_text(stdout_stream.str());
        std::string line;
        std::getline(stderr_stream, line);
        if(!line.empty() && line != "NaN") {
          try {
            auto offset = atoi(line.c_str());
            if(offset < get_buffer()->size()) {
              get_buffer()->place_cursor(get_buffer()->get_iter_at_offset(offset));
              hide_tooltips();
            }
          }
          catch(...) {
          }
        }
      }
      else if(is_generic_view) {
        static std::regex regex(R"(^\[error\] stdin: (.*) \(([0-9]*):([0-9]*)\)$)");
        std::string line;
        std::getline(stderr_stream, line);
        std::smatch sm;
        if(std::regex_match(line, sm, regex)) {
          try {
            auto start = get_iter_at_line_offset(atoi(sm[2].str().c_str()) - 1, atoi(sm[3].str().c_str()) - 1);
            ++num_errors;
            while(start.ends_line() && start.backward_char()) {
            }
            auto end = start;
            end.forward_char();
            if(start == end)
              start.backward_char();

            add_diagnostic_tooltip(start, end, true, [error_message = sm[1].str()](const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
              buffer->insert_at_cursor(error_message);
            });
          }
          catch(...) {
          }
        }
      }
      if(is_generic_view) {
        status_diagnostics = std::make_tuple(num_warnings, num_errors, num_fix_its);
        if(update_status_diagnostics)
          update_status_diagnostics(this);
      }
    };
  }
  else if(is_bracket_language) {
    format_style = [this](bool continue_without_style_file) {
      static auto clang_format_command = filesystem::get_executable("clang-format").string();

      auto command = clang_format_command + " -output-replacements-xml -assume-filename=" + filesystem::escape_argument(this->file_path.string());

      if(get_buffer()->get_has_selection()) {
        Gtk::TextIter start, end;
        get_buffer()->get_selection_bounds(start, end);
        command += " -lines=" + std::to_string(start.get_line() + 1) + ':' + std::to_string(end.get_line() + 1);
      }

      bool use_style_file = false;
      auto style_file_search_path = this->file_path.parent_path();
      boost::system::error_code ec;
      while(true) {
        if(boost::filesystem::exists(style_file_search_path / ".clang-format", ec) || boost::filesystem::exists(style_file_search_path / "_clang-format", ec)) {
          use_style_file = true;
          break;
        }
        if(style_file_search_path == style_file_search_path.root_directory())
          break;
        style_file_search_path = style_file_search_path.parent_path();
      }

      if(use_style_file)
        command += " -style=file";
      else {
        if(!continue_without_style_file)
          return;
        unsigned indent_width;
        std::string tab_style;
        if(tab_char == '\t') {
          indent_width = tab_size * 8;
          tab_style = "UseTab: Always";
        }
        else {
          indent_width = tab_size;
          tab_style = "UseTab: Never";
        }
        command += " -style=\"{IndentWidth: " + std::to_string(indent_width);
        command += ", " + tab_style;
        command += ", " + std::string("AccessModifierOffset: -") + std::to_string(indent_width);
        if(Config::get().source.clang_format_style != "")
          command += ", " + Config::get().source.clang_format_style;
        command += "}\"";
      }

      std::stringstream stdin_stream(get_buffer()->get_text()), stdout_stream;

      auto exit_status = Terminal::get().process(stdin_stream, stdout_stream, command, this->file_path.parent_path());
      if(exit_status == 0) {
        // The following code is complex due to clang-format returning offsets in byte offsets instead of char offsets

        // Create bytes_in_lines cache to significantly speed up the processing of finding iterators from byte offsets
        std::vector<size_t> bytes_in_lines;
        auto line_count = get_buffer()->get_line_count();
        for(int line_nr = 0; line_nr < line_count; ++line_nr) {
          auto iter = get_buffer()->get_iter_at_line(line_nr);
          bytes_in_lines.emplace_back(iter.get_bytes_in_line());
        }

        get_buffer()->begin_user_action();
        try {
          boost::property_tree::ptree pt;
          boost::property_tree::xml_parser::read_xml(stdout_stream, pt);
          auto replacements_pt = pt.get_child("replacements");
          for(auto it = replacements_pt.rbegin(); it != replacements_pt.rend(); ++it) {
            if(it->first == "replacement") {
              auto offset = it->second.get<size_t>("<xmlattr>.offset");
              auto length = it->second.get<size_t>("<xmlattr>.length");
              auto replacement_str = it->second.get<std::string>("");

              size_t bytes = 0;
              for(size_t c = 0; c < bytes_in_lines.size(); ++c) {
                auto previous_bytes = bytes;
                bytes += bytes_in_lines[c];
                if(offset < bytes || (c == bytes_in_lines.size() - 1 && offset == bytes)) {
                  std::pair<size_t, size_t> line_index(c, offset - previous_bytes);
                  auto start = get_buffer()->get_iter_at_line_index(line_index.first, line_index.second);

                  // Use left gravity insert to avoid moving cursor from end of line
                  bool left_gravity_insert = false;
                  if(get_buffer()->get_insert()->get_iter() == start) {
                    auto iter = start;
                    do {
                      if(*iter != ' ' && *iter != '\t') {
                        left_gravity_insert = iter.ends_line();
                        break;
                      }
                    } while(iter.forward_char());
                  }

                  if(length > 0) {
                    auto offset_end = offset + length;
                    size_t bytes = 0;
                    for(size_t c = 0; c < bytes_in_lines.size(); ++c) {
                      auto previous_bytes = bytes;
                      bytes += bytes_in_lines[c];
                      if(offset_end < bytes || (c == bytes_in_lines.size() - 1 && offset_end == bytes)) {
                        auto end = get_buffer()->get_iter_at_line_index(c, offset_end - previous_bytes);
                        get_buffer()->erase(start, end);
                        start = get_buffer()->get_iter_at_line_index(line_index.first, line_index.second);
                        break;
                      }
                    }
                  }
                  if(left_gravity_insert) {
                    auto mark = get_buffer()->create_mark(start);
                    get_buffer()->insert(start, replacement_str);
                    get_buffer()->place_cursor(mark->get_iter());
                    get_buffer()->delete_mark(mark);
                  }
                  else
                    get_buffer()->insert(start, replacement_str);
                  break;
                }
              }
            }
          }
        }
        catch(const std::exception &e) {
          Terminal::get().print(std::string("Error: error parsing clang-format output: ") + e.what() + '\n', true);
        }
        get_buffer()->end_user_action();
      }
    };
  }
  else if(language && language->get_id() == "markdown") {
    // The style file currently has no options, but checking if it exists
    format_style = [this](bool continue_without_style_file) {
      bool has_style_file = false;
      auto style_file_search_path = this->file_path.parent_path();
      while(true) {
        if(boost::filesystem::exists(style_file_search_path / ".markdown-format")) {
          has_style_file = true;
          break;
        }
        if(style_file_search_path == style_file_search_path.root_directory())
          break;
        style_file_search_path = style_file_search_path.parent_path();
      }
      if(!has_style_file && !continue_without_style_file)
        return;

      auto special_character = [](Gtk::TextIter iter) {
        if(*iter == '*' || *iter == '#' || *iter == '<' || *iter == '>' || *iter == ' ' ||
           *iter == '=' || *iter == '`' || *iter == '-' || *iter == '|')
          return true;
        // Tests if a line starts with for instance: 2.
        if(*iter >= '0' && *iter <= '9' && iter.forward_char() &&
           *iter == '.' && iter.forward_char() &&
           *iter == ' ')
          return true;
        return false;
      };

      get_buffer()->begin_user_action();
      disable_spellcheck = true;
      cleanup_whitespace_characters();

      auto iter = get_buffer()->begin();
      size_t last_space_offset = -1;
      bool headline = false;
      bool monospace = false;
      bool script = false;
      bool html_tag = false;
      bool table = false;
      int square_brackets = 0;
      do {
        if(iter.starts_line()) {
          last_space_offset = -1;
          auto next_line_iter = iter;
          if(*iter == '#' || (next_line_iter.forward_line() && *next_line_iter == '='))
            headline = true;
          else
            headline = false;
          auto test_iter = iter;
          if(*test_iter == '`' && test_iter.forward_char() &&
             *test_iter == '`' && test_iter.forward_char() &&
             *test_iter == '`') {
            script = !script;
            iter.forward_chars(3);
            continue;
          }
          table = *iter == '|';
        }
        if(!script && *iter == '`')
          monospace = !monospace;
        if(!script && !monospace) {
          if(*iter == '<')
            html_tag = true;
          else if(*iter == '>')
            html_tag = false;
          else if(*iter == '[')
            ++square_brackets;
          else if(*iter == ']')
            --square_brackets;
        }
        if(!headline && !script && !monospace && !html_tag && !table && square_brackets == 0) {
          if(*iter == ' ' && iter.get_line_offset() <= 80)
            last_space_offset = iter.get_offset();
          // Insert newline on long lines
          else if((*iter == ' ' || iter.ends_line()) && iter.get_line_offset() > 80 && last_space_offset != static_cast<size_t>(-1)) {
            auto stored_iter = iter;
            iter = get_buffer()->get_iter_at_offset(last_space_offset);
            auto next_iter = iter;
            next_iter.forward_char();
            // Do not add newline if the next iter is a special character
            if(special_character(next_iter)) {
              iter = stored_iter;
              if(*iter == ' ')
                last_space_offset = iter.get_offset();
              continue;
            }
            iter = get_buffer()->erase(iter, next_iter);
            iter = get_buffer()->insert(iter, "\n");
            iter.backward_char();
          }
          // Remove newline on short lines
          else if(iter.ends_line() && !iter.starts_line() && iter.get_line_offset() <= 80) {
            auto next_line_iter = iter;
            // Do not remove newline if the next line for instance is a header
            if(next_line_iter.forward_char() && !next_line_iter.ends_line() && !special_character(next_line_iter)) {
              auto end_word_iter = next_line_iter;
              // Do not remove newline if the word on the next line is too long
              size_t diff = 0;
              while(*end_word_iter != ' ' && !end_word_iter.ends_line() && end_word_iter.forward_char())
                ++diff;
              if(iter.get_line_offset() + diff + 1 <= 80) {
                iter = get_buffer()->erase(iter, next_line_iter);
                iter = get_buffer()->insert(iter, " ");
                iter.backward_char();
                if(iter.get_line_offset() <= 80)
                  last_space_offset = iter.get_offset();
              }
            }
          }
        }
      } while(iter.forward_char());
      disable_spellcheck = false;
      get_buffer()->end_user_action();
    };
  }
}

Source::View::~View() {
  delayed_tooltips_connection.disconnect();
  delayed_tag_similar_symbols_connection.disconnect();
  delayed_tag_clickable_connection.disconnect();
  renderer_activate_connection.disconnect();

  non_deleted_views.erase(this);
  views.erase(this);
}

void Source::View::hide_tooltips() {
  delayed_tooltips_connection.disconnect();
  type_tooltips.hide();
  diagnostic_tooltips.hide();
}

void Source::View::hide_dialogs() {
  SpellCheckView::hide_dialogs();
  if(SelectionDialog::get())
    SelectionDialog::get()->hide();
  if(CompletionDialog::get())
    CompletionDialog::get()->hide();
}

void Source::View::insert_with_links_tagged(const Glib::RefPtr<Gtk::TextBuffer> &buffer, const std::string &text) {
  static std::regex http_regex("(https?://[a-zA-Z0-9\\-._~:/?#\\[\\]@!$&'()*+,;=]+[a-zA-Z0-9\\-_~/@$*+;=])");
  std::smatch sm;
  std::sregex_iterator it(text.begin(), text.end(), http_regex);
  std::sregex_iterator end;
  size_t start_pos = 0;
  for(; it != end; ++it) {
    buffer->insert(buffer->get_insert()->get_iter(), &text[start_pos], &text[it->position()]);
    buffer->insert_with_tag(buffer->get_insert()->get_iter(), &text[it->position()], &text[it->position() + it->length()], link_tag);
    start_pos = it->position() + it->length();
  }
  buffer->insert(buffer->get_insert()->get_iter(), &text[start_pos], &text[text.size()]);
}

void Source::View::add_diagnostic_tooltip(const Gtk::TextIter &start, const Gtk::TextIter &end, bool error, std::function<void(const Glib::RefPtr<Gtk::TextBuffer> &)> &&set_buffer) {
  diagnostic_offsets.emplace(start.get_offset());

  std::string severity_tag_name = error ? "def:error" : "def:warning";

  diagnostic_tooltips.emplace_back(this, get_buffer()->create_mark(start), get_buffer()->create_mark(end), [error, severity_tag_name, set_buffer = std::move(set_buffer)](const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
    buffer->insert_with_tag(buffer->get_insert()->get_iter(), error ? "Error" : "Warning", severity_tag_name);
    buffer->insert(buffer->get_insert()->get_iter(), ":\n");
    set_buffer(buffer);
  });

  get_buffer()->apply_tag_by_name(severity_tag_name + "_underline", start, end);

  auto iter = get_buffer()->get_insert()->get_iter();
  if(iter.ends_line()) {
    auto next_iter = iter;
    if(next_iter.forward_char())
      get_buffer()->remove_tag_by_name(severity_tag_name + "_underline", iter, next_iter);
  }
}

void Source::View::clear_diagnostic_tooltips() {
  diagnostic_offsets.clear();
  diagnostic_tooltips.clear();
  get_buffer()->remove_tag_by_name("def:warning_underline", get_buffer()->begin(), get_buffer()->end());
  get_buffer()->remove_tag_by_name("def:error_underline", get_buffer()->begin(), get_buffer()->end());
}

void Source::View::place_cursor_at_next_diagnostic() {
  auto insert_offset = get_buffer()->get_insert()->get_iter().get_offset();
  for(auto offset : diagnostic_offsets) {
    if(offset > insert_offset) {
      get_buffer()->place_cursor(get_buffer()->get_iter_at_offset(offset));
      scroll_to(get_buffer()->get_insert(), 0.0, 1.0, 0.5);
      return;
    }
  }
  if(diagnostic_offsets.size() == 0)
    Info::get().print("No diagnostics found in current buffer");
  else {
    auto iter = get_buffer()->get_iter_at_offset(*diagnostic_offsets.begin());
    get_buffer()->place_cursor(iter);
    scroll_to(get_buffer()->get_insert(), 0.0, 1.0, 0.5);
  }
}

Gtk::TextIter Source::View::find_non_whitespace_code_iter_backward(Gtk::TextIter iter) {
  if(iter.starts_line())
    return iter;
  while(!iter.starts_line() && (!is_code_iter(iter) || *iter == ' ' || *iter == '\t' || iter.ends_line()) && iter.backward_char()) {
  }
  return iter;
}

Gtk::TextIter Source::View::get_start_of_expression(Gtk::TextIter iter) {
  while(!iter.starts_line() && (*iter == ' ' || *iter == '\t' || iter.ends_line() || !is_code_iter(iter)) && iter.backward_char()) {
  }

  if(iter.starts_line())
    return iter;

  bool has_semicolon = false;
  bool has_open_curly = false;

  if(is_bracket_language) {
    if(*iter == ';' && is_code_iter(iter))
      has_semicolon = true;
    if(*iter == '{' && is_code_iter(iter)) {
      iter.backward_char();
      has_open_curly = true;
    }
  }

  int para_count = 0;
  int square_count = 0;
  long curly_count = 0;

  do {
    if(*iter == '(' && is_code_iter(iter))
      para_count++;
    else if(*iter == ')' && is_code_iter(iter))
      para_count--;
    else if(*iter == '[' && is_code_iter(iter))
      square_count++;
    else if(*iter == ']' && is_code_iter(iter))
      square_count--;
    else if(*iter == '{' && is_code_iter(iter))
      curly_count++;
    else if(*iter == '}' && is_code_iter(iter)) {
      curly_count--;
      if(iter.starts_line())
        break;
    }

    if(para_count > 0 || square_count > 0 || curly_count > 0)
      break;

    if(iter.starts_line() && para_count == 0 && square_count == 0) {
      if(!is_bracket_language)
        return iter;
      // Handle << at the beginning of the sentence if iter initially started with ;
      if(has_semicolon) {
        auto test_iter = get_tabs_end_iter(iter);
        if(!test_iter.starts_line() && *test_iter == '<' && is_code_iter(test_iter) &&
           test_iter.forward_char() && *test_iter == '<')
          continue;
      }
      // Handle for instance: test\n  .test();
      if(has_semicolon && use_fixed_continuation_indenting) {
        auto test_iter = get_tabs_end_iter(iter);
        if(!test_iter.starts_line() && *test_iter == '.' && is_code_iter(test_iter))
          continue;
      }
      // Handle : at the beginning of the sentence if iter initially started with {
      if(has_open_curly) {
        auto test_iter = get_tabs_end_iter(iter);
        if(!test_iter.starts_line() && *test_iter == ':' && is_code_iter(test_iter))
          continue;
      }
      // Handle ',', ':', or operators that can be used between two lines, on previous line:
      auto previous_iter = iter;
      previous_iter.backward_char();
      while(!previous_iter.starts_line() && (*previous_iter == ' ' || previous_iter.ends_line() || !is_code_iter(previous_iter)) && previous_iter.backward_char()) {
      }
      if(previous_iter.starts_line())
        return iter;
      // Handle for instance: Test::Test():\n    test(2) {
      if(has_open_curly && *previous_iter == ':') {
        previous_iter.backward_char();
        while(!previous_iter.starts_line() && *previous_iter == ' ' && previous_iter.backward_char()) {
        }
        if(*previous_iter == ')') {
          auto token = get_token(get_tabs_end_iter(previous_iter));
          if(token != "case")
            continue;
        }
        return iter;
      }
      // Handle for instance: int a =\n    b;
      if(*previous_iter == '=' || *previous_iter == '+' || *previous_iter == '-' || *previous_iter == '*' || *previous_iter == '/' ||
         *previous_iter == '%' || *previous_iter == '<' || *previous_iter == '>' || *previous_iter == '&' || *previous_iter == '|') {
        if(has_semicolon)
          continue;
        return iter;
      }
      if(*previous_iter != ',')
        return iter;
    }
  } while(iter.backward_char());

  return iter;
}

bool Source::View::find_open_curly_bracket_backward(Gtk::TextIter iter, Gtk::TextIter &found_iter) {
  long count = 0;

  do {
    if(*iter == '{') {
      if(count == 0 && is_code_iter(iter)) {
        found_iter = iter;
        return true;
      }
      count++;
    }
    else if(*iter == '}' && is_code_iter(iter))
      count--;
  } while(iter.backward_char());
  return false;
}

bool Source::View::find_close_symbol_forward(Gtk::TextIter iter, Gtk::TextIter &found_iter, unsigned int positive_char, unsigned int negative_char) {
  long count = 0;
  if(positive_char == '{' && negative_char == '}') {
    do {
      if(*iter == negative_char && is_code_iter(iter)) {
        if(count == 0) {
          found_iter = iter;
          return true;
        }
        count--;
      }
      else if(*iter == positive_char && is_code_iter(iter))
        count++;
    } while(iter.forward_char());
    return false;
  }
  else {
    long curly_count = 0;
    do {
      if(*iter == positive_char && is_code_iter(iter))
        count++;
      else if(*iter == negative_char && is_code_iter(iter)) {
        if(count == 0) {
          found_iter = iter;
          return true;
        }
        count--;
      }
      else if(*iter == '{' && is_code_iter(iter))
        curly_count++;
      else if(*iter == '}' && is_code_iter(iter)) {
        if(curly_count == 0)
          return false;
        curly_count--;
      }
    } while(iter.forward_char());
    return false;
  }
}

long Source::View::symbol_count(Gtk::TextIter iter, unsigned int positive_char, unsigned int negative_char) {
  auto iter_stored = iter;
  long symbol_count = 0;

  if(positive_char == '{' && negative_char == '}') {
    // If checking top-level curly brackets, check whole buffer
    auto previous_iter = iter;
    if(iter.starts_line() || (previous_iter.backward_char() && previous_iter.starts_line() && *previous_iter == '{')) {
      auto iter = get_buffer()->begin();
      do {
        if(*iter == '{' && is_code_iter(iter))
          ++symbol_count;
        else if(*iter == '}' && is_code_iter(iter))
          --symbol_count;
      } while(iter.forward_char());
      return symbol_count;
    }
    // Can stop when text is found at top-level indentation
    else {
      do {
        if(*iter == '{' && is_code_iter(iter))
          ++symbol_count;
        else if(*iter == '}' && is_code_iter(iter))
          --symbol_count;
        if(iter.starts_line() && !iter.ends_line() && *iter != '#' && *iter != ' ' && *iter != '\t')
          break;
      } while(iter.backward_char());

      iter = iter_stored;
      if(!iter.forward_char())
        return symbol_count;

      do {
        if(*iter == '{' && is_code_iter(iter))
          ++symbol_count;
        else if(*iter == '}' && is_code_iter(iter))
          --symbol_count;
        if(iter.starts_line() && !iter.ends_line() && *iter != '#' && *iter != ' ' && *iter != '\t') {
          if(*iter == 'p') {
            auto token = get_token(iter);
            if(token == "public" || token == "protected" || token == "private")
              continue;
          }
          break;
        }
      } while(iter.forward_char());
      return symbol_count;
    }
  }

  long curly_count = 0;

  do {
    if(*iter == positive_char && is_code_iter(iter))
      symbol_count++;
    else if(*iter == negative_char && is_code_iter(iter))
      symbol_count--;
    else if(*iter == '{' && is_code_iter(iter)) {
      if(curly_count == 0)
        break;
      curly_count++;
    }
    else if(*iter == '}' && is_code_iter(iter))
      curly_count--;
  } while(iter.backward_char());

  iter = iter_stored;
  if(!iter.forward_char())
    return symbol_count;

  curly_count = 0;
  do {
    if(*iter == positive_char && is_code_iter(iter))
      symbol_count++;
    else if(*iter == negative_char && is_code_iter(iter))
      symbol_count--;
    else if(*iter == '{' && is_code_iter(iter))
      curly_count++;
    else if(*iter == '}' && is_code_iter(iter)) {
      if(curly_count == 0)
        break;
      curly_count--;
    }
  } while(iter.forward_char());

  return symbol_count;
}

bool Source::View::is_templated_function(Gtk::TextIter iter, Gtk::TextIter &parenthesis_end_iter) {
  auto iter_stored = iter;
  long bracket_count = 0;
  long curly_count = 0;

  if(!(iter.backward_char() && *iter == '>' && *iter_stored == '('))
    return false;

  do {
    if(*iter == '<' && is_code_iter(iter))
      bracket_count++;
    else if(*iter == '>' && is_code_iter(iter))
      bracket_count--;
    else if(*iter == '{' && is_code_iter(iter))
      curly_count++;
    else if(*iter == '}' && is_code_iter(iter))
      curly_count--;

    if(bracket_count == 0)
      break;

    if(curly_count > 0)
      break;
  } while(iter.backward_char());

  if(bracket_count != 0)
    return false;

  iter = iter_stored;
  bracket_count = 0;
  curly_count = 0;
  do {
    if(*iter == '(' && is_code_iter(iter))
      bracket_count++;
    else if(*iter == ')' && is_code_iter(iter))
      bracket_count--;
    else if(*iter == '{' && is_code_iter(iter))
      curly_count++;
    else if(*iter == '}' && is_code_iter(iter))
      curly_count--;

    if(bracket_count == 0) {
      parenthesis_end_iter = iter;
      return true;
    }

    if(curly_count < 0)
      return false;
  } while(iter.forward_char());

  return false;
}

bool Source::View::is_possible_argument() {
  auto iter = get_buffer()->get_insert()->get_iter();
  if(iter.backward_char() && (!interactive_completion || last_keyval == '(' || last_keyval == ',' || last_keyval == ' ' ||
                              last_keyval == GDK_KEY_Return || last_keyval == GDK_KEY_KP_Enter)) {
    while((*iter == ' ' || *iter == '\t' || *iter == '\n' || *iter == '\r') && iter.backward_char()) {
    }
    if(*iter == '(' || *iter == ',')
      return true;
  }
  return false;
}

bool Source::View::on_key_press_event(GdkEventKey *key) {
  enable_multiple_cursors = true;
  ScopeGuard guard{[this] {
    enable_multiple_cursors = false;
  }};

  if(SelectionDialog::get() && SelectionDialog::get()->is_visible()) {
    if(SelectionDialog::get()->on_key_press(key))
      return true;
  }
  if(CompletionDialog::get() && CompletionDialog::get()->is_visible()) {
    if(CompletionDialog::get()->on_key_press(key))
      return true;
  }

  if(last_keyval < GDK_KEY_Shift_L || last_keyval > GDK_KEY_Hyper_R)
    previous_non_modifier_keyval = last_keyval;
  last_keyval = key->keyval;

  if((key->keyval == GDK_KEY_Tab || key->keyval == GDK_KEY_ISO_Left_Tab) && (key->state & GDK_SHIFT_MASK) == 0 && select_snippet_argument())
    return true;
  else if(key->keyval == GDK_KEY_Escape && clear_snippet_marks())
    return true;
  else if(Config::get().source.enable_multiple_cursors && on_key_press_event_extra_cursors(key))
    return true;

  //Move cursor one paragraph down
  if((key->keyval == GDK_KEY_Down || key->keyval == GDK_KEY_KP_Down) && (key->state & GDK_CONTROL_MASK) > 0) {
    auto selection_start_iter = get_buffer()->get_selection_bound()->get_iter();
    auto iter = get_buffer()->get_iter_at_line(get_buffer()->get_insert()->get_iter().get_line());
    bool empty_line = false;
    bool text_found = false;
    for(;;) {
      if(!iter)
        break;
      if(iter.starts_line())
        empty_line = true;
      if(empty_line && !iter.ends_line() && *iter != '\n' && *iter != ' ' && *iter != '\t')
        empty_line = false;
      if(!text_found && !iter.ends_line() && *iter != '\n' && *iter != ' ' && *iter != '\t')
        text_found = true;
      if(empty_line && text_found && iter.ends_line())
        break;
      iter.forward_char();
    }
    iter = get_buffer()->get_iter_at_line(iter.get_line());
    if((key->state & GDK_SHIFT_MASK) > 0)
      get_buffer()->select_range(iter, selection_start_iter);
    else
      get_buffer()->place_cursor(iter);
    scroll_to(get_buffer()->get_insert());
    return true;
  }
  //Move cursor one paragraph up
  else if((key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_KP_Up) && (key->state & GDK_CONTROL_MASK) > 0) {
    auto selection_start_iter = get_buffer()->get_selection_bound()->get_iter();
    auto iter = get_buffer()->get_iter_at_line(get_buffer()->get_insert()->get_iter().get_line());
    iter.backward_char();
    bool empty_line = false;
    bool text_found = false;
    bool move_to_start = false;
    for(;;) {
      if(!iter)
        break;
      if(iter.ends_line())
        empty_line = true;
      if(empty_line && !iter.ends_line() && *iter != '\n' && *iter != ' ' && *iter != '\t')
        empty_line = false;
      if(!text_found && !iter.ends_line() && *iter != '\n' && *iter != ' ' && *iter != '\t')
        text_found = true;
      if(empty_line && text_found && iter.starts_line())
        break;
      if(iter.is_start()) {
        move_to_start = true;
        break;
      }
      iter.backward_char();
    }
    if(empty_line && !move_to_start) {
      iter = get_iter_at_line_end(iter.get_line());
      iter.forward_char();
      if(!iter.starts_line()) // For CR+LF
        iter.forward_char();
    }
    if((key->state & GDK_SHIFT_MASK) > 0)
      get_buffer()->select_range(iter, selection_start_iter);
    else
      get_buffer()->place_cursor(iter);
    scroll_to(get_buffer()->get_insert());
    return true;
  }

  get_buffer()->begin_user_action();

  // Shift+enter: go to end of line and enter
  if((key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) && (key->state & GDK_SHIFT_MASK) > 0) {
    auto iter = get_buffer()->get_insert()->get_iter();
    if(!iter.ends_line()) {
      iter.forward_to_line_end();
      get_buffer()->place_cursor(iter);
    }
  }

  if(Config::get().source.smart_brackets && on_key_press_event_smart_brackets(key)) {
    get_buffer()->end_user_action();
    return true;
  }
  if(Config::get().source.smart_inserts && on_key_press_event_smart_inserts(key)) {
    get_buffer()->end_user_action();
    return true;
  }

  if(is_bracket_language && on_key_press_event_bracket_language(key)) {
    get_buffer()->end_user_action();
    return true;
  }
  else if(on_key_press_event_basic(key)) {
    get_buffer()->end_user_action();
    return true;
  }
  else {
    get_buffer()->end_user_action();
    return Gsv::View::on_key_press_event(key);
  }
}

// Basic indentation
bool Source::View::on_key_press_event_basic(GdkEventKey *key) {
  auto iter = get_buffer()->get_insert()->get_iter();

  // Indent as in current or next line
  if((key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) && !get_buffer()->get_has_selection() && !iter.starts_line()) {
    cleanup_whitespace_characters(iter);

    iter = get_buffer()->get_insert()->get_iter();
    auto condition_iter = iter;
    condition_iter.backward_char();
    condition_iter = find_non_whitespace_code_iter_backward(condition_iter);
    auto start_iter = get_start_of_expression(condition_iter);
    auto tabs_end_iter = get_tabs_end_iter(start_iter);
    auto tabs = get_line_before(tabs_end_iter);

    // Python indenting after :
    if(*condition_iter == ':' && language && language->get_id() == "python") {
      get_buffer()->insert_at_cursor('\n' + tabs + tab);
      scroll_to(get_buffer()->get_insert());
      return true;
    }

    // Indent as in current or next line
    int line_nr = iter.get_line();
    if(iter.ends_line() && (line_nr + 1) < get_buffer()->get_line_count()) {
      auto next_tabs_end_iter = get_tabs_end_iter(line_nr + 1);
      if(next_tabs_end_iter.get_line_offset() > tabs_end_iter.get_line_offset()) {
        get_buffer()->insert_at_cursor('\n' + get_line_before(next_tabs_end_iter));
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    get_buffer()->insert_at_cursor('\n' + tabs);
    scroll_to(get_buffer()->get_insert());
    return true;
  }
  // Indent as in next or previous line
  else if(key->keyval == GDK_KEY_Tab && (key->state & GDK_SHIFT_MASK) == 0) {
    // Special case if insert is at beginning of empty line:
    if(iter.starts_line() && iter.ends_line() && !get_buffer()->get_has_selection()) {
      auto prev_line_iter = iter;
      while(prev_line_iter.starts_line() && prev_line_iter.backward_char()) {
      }
      auto start_iter = get_start_of_expression(prev_line_iter);
      auto prev_line_tabs_end_iter = get_tabs_end_iter(start_iter);

      auto next_line_iter = iter;
      while(next_line_iter.starts_line() && next_line_iter.forward_char()) {
      }
      auto next_line_tabs_end_iter = get_tabs_end_iter(next_line_iter);

      Gtk::TextIter tabs_end_iter;
      if(next_line_tabs_end_iter.get_line_offset() > prev_line_tabs_end_iter.get_line_offset())
        tabs_end_iter = next_line_tabs_end_iter;
      else
        tabs_end_iter = prev_line_tabs_end_iter;
      auto tabs = get_line_before(tabs_end_iter);

      get_buffer()->insert_at_cursor(tabs.size() >= tab_size ? tabs : tab);
      return true;
    }

    if(!Config::get().source.tab_indents_line && !get_buffer()->get_has_selection()) {
      get_buffer()->insert_at_cursor(tab);
      return true;
    }

    // Indent right when clicking tab, no matter where in the line the cursor is. Also works on selected text.
    Gtk::TextIter selection_start, selection_end;
    get_buffer()->get_selection_bounds(selection_start, selection_end);
    auto selection_end_mark = get_buffer()->create_mark(selection_end);
    int line_start = selection_start.get_line();
    int line_end = selection_end.get_line();
    for(int line = line_start; line <= line_end; line++) {
      auto line_it = get_buffer()->get_iter_at_line(line);
      if(!get_buffer()->get_has_selection() || line_it != selection_end_mark->get_iter())
        get_buffer()->insert(line_it, tab);
    }
    get_buffer()->delete_mark(selection_end_mark);
    return true;
  }
  // Indent left when clicking shift-tab, no matter where in the line the cursor is. Also works on selected text.
  else if((key->keyval == GDK_KEY_ISO_Left_Tab || key->keyval == GDK_KEY_Tab) && (key->state & GDK_SHIFT_MASK) > 0) {
    Gtk::TextIter selection_start, selection_end;
    get_buffer()->get_selection_bounds(selection_start, selection_end);
    int line_start = selection_start.get_line();
    int line_end = selection_end.get_line();

    unsigned indent_left_steps = tab_size;
    std::vector<bool> ignore_line;
    for(int line_nr = line_start; line_nr <= line_end; line_nr++) {
      auto line_it = get_buffer()->get_iter_at_line(line_nr);
      if(!get_buffer()->get_has_selection() || line_it != selection_end) {
        auto tabs_end_iter = get_tabs_end_iter(line_nr);
        if(tabs_end_iter.starts_line() && tabs_end_iter.ends_line())
          ignore_line.push_back(true);
        else {
          auto line_tabs = get_line_before(tabs_end_iter);

          if(line_tabs.size() > 0) {
            indent_left_steps = std::min(indent_left_steps, static_cast<unsigned>(line_tabs.size()));
            ignore_line.push_back(false);
          }
          else
            return true;
        }
      }
    }

    for(int line_nr = line_start; line_nr <= line_end; line_nr++) {
      Gtk::TextIter line_it = get_buffer()->get_iter_at_line(line_nr);
      Gtk::TextIter line_plus_it = line_it;
      if(!get_buffer()->get_has_selection() || line_it != selection_end) {
        line_plus_it.forward_chars(indent_left_steps);
        if(!ignore_line.at(line_nr - line_start))
          get_buffer()->erase(line_it, line_plus_it);
      }
    }
    return true;
  }
  // "Smart" backspace key
  else if(key->keyval == GDK_KEY_BackSpace && !get_buffer()->get_has_selection()) {
    auto line = get_line_before();
    bool do_smart_backspace = true;
    for(auto &chr : line) {
      if(chr != ' ' && chr != '\t') {
        do_smart_backspace = false;
        break;
      }
    }
    if(iter.get_line() == 0) // Special case since there are no previous line
      do_smart_backspace = false;
    if(do_smart_backspace) {
      auto previous_line_end_iter = iter;
      if(previous_line_end_iter.backward_chars(line.size() + 1)) {
        if(!previous_line_end_iter.ends_line()) // For CR+LF
          previous_line_end_iter.backward_char();
        if(previous_line_end_iter.starts_line()) // When previous line is empty, keep tabs in current line
          get_buffer()->erase(previous_line_end_iter, get_buffer()->get_iter_at_line(iter.get_line()));
        else
          get_buffer()->erase(previous_line_end_iter, iter);
        return true;
      }
    }
  }
  // "Smart" delete key
  else if(key->keyval == GDK_KEY_Delete && !get_buffer()->get_has_selection()) {
    auto insert_iter = iter;
    bool do_smart_delete = true;
    do {
      if(*iter != ' ' && *iter != '\t' && !iter.ends_line()) {
        do_smart_delete = false;
        break;
      }
      if(iter.ends_line()) {
        if(*iter == '\r') // For CR+LF
          iter.forward_char();
        if(!iter.forward_char())
          do_smart_delete = false;
        break;
      }
    } while(iter.forward_char());
    if(do_smart_delete) {
      if(!insert_iter.starts_line()) {
        while((*iter == ' ' || *iter == '\t') && iter.forward_char()) {
        }
      }
      get_buffer()->erase(insert_iter, iter);
      return true;
    }
  }
  // Smart Home/End-keys
  else if((key->keyval == GDK_KEY_Home || key->keyval == GDK_KEY_KP_Home) && (key->state & GDK_CONTROL_MASK) == 0) {
    if((key->state & GDK_SHIFT_MASK) > 0)
      get_buffer()->move_mark_by_name("insert", get_smart_home_iter(iter));
    else
      get_buffer()->place_cursor(get_smart_home_iter(iter));
    scroll_to(get_buffer()->get_insert());
    return true;
  }
  else if((key->keyval == GDK_KEY_End || key->keyval == GDK_KEY_KP_End) && (key->state & GDK_CONTROL_MASK) == 0) {
    if((key->state & GDK_SHIFT_MASK) > 0)
      get_buffer()->move_mark_by_name("insert", get_smart_end_iter(iter));
    else
      get_buffer()->place_cursor(get_smart_end_iter(iter));
    scroll_to(get_buffer()->get_insert());
    return true;
  }

  // Workaround for TextView::on_key_press_event bug sometimes causing segmentation faults
  // TODO: figure out the bug and create pull request to gtk
  // Have only experienced this on OS X
  // Note: valgrind reports issues on TextView::on_key_press_event as well
  auto unicode = gdk_keyval_to_unicode(key->keyval);
  if((key->state & (GDK_CONTROL_MASK | GDK_META_MASK)) == 0 && unicode >= 32 && unicode != 127 &&
     (previous_non_modifier_keyval < GDK_KEY_dead_grave || previous_non_modifier_keyval > GDK_KEY_dead_greek)) {
    if(get_buffer()->get_has_selection()) {
      Gtk::TextIter selection_start, selection_end;
      get_buffer()->get_selection_bounds(selection_start, selection_end);
      get_buffer()->erase(selection_start, selection_end);
    }
    get_buffer()->insert_at_cursor(Glib::ustring(1, unicode));
    scroll_to(get_buffer()->get_insert());

    // Trick to make the cursor visible right after insertion:
    set_cursor_visible(false);
    set_cursor_visible();

    return true;
  }

  return false;
}

//Bracket language indentation
bool Source::View::on_key_press_event_bracket_language(GdkEventKey *key) {
  const static std::regex no_bracket_statement_regex("^[ \t]*(if( +constexpr)?|for|while) *\\(.*[^;}{] *$|"
                                                     "^[ \t]*[}]? *else if( +constexpr)? *\\(.*[^;}{] *$|"
                                                     "^[ \t]*[}]? *else *$", std::regex::extended);

  auto iter = get_buffer()->get_insert()->get_iter();

  if(get_buffer()->get_has_selection())
    return false;

  if(!is_code_iter(iter)) {
    // Add * at start of line in comment blocks
    if(key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) {
      if(!iter.starts_line() && (!string_tag || (!iter.has_tag(string_tag) && !iter.ends_tag(string_tag)))) {
        cleanup_whitespace_characters(iter);
        iter = get_buffer()->get_insert()->get_iter();

        auto start_iter = get_tabs_end_iter(iter.get_line());
        auto end_iter = start_iter;
        end_iter.forward_chars(2);
        auto start_of_sentence = get_buffer()->get_text(start_iter, end_iter);
        if(!start_of_sentence.empty()) {
          if(start_of_sentence == "/*" || start_of_sentence[0] == '*') {
            auto tabs = get_line_before(start_iter);
            auto insert_str = '\n' + tabs;
            if(start_of_sentence[0] == '/')
              insert_str += ' ';
            insert_str += "* ";

            get_buffer()->insert_at_cursor(insert_str);
            return true;
          }
        }
      }
      else if(!comment_tag || !iter.ends_tag(comment_tag))
        return false;
    }
    else
      return false;
  }

  // Indent depending on if/else/etc and brackets
  if((key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) && !iter.starts_line()) {
    cleanup_whitespace_characters(iter);
    iter = get_buffer()->get_insert()->get_iter();

    auto previous_iter = iter;
    previous_iter.backward_char();
    // Remove matching bracket highlights that get extended when inserting text in between the brackets
    ScopeGuard guard;
    if((*previous_iter == '{' && *iter == '}') || (*previous_iter == '(' && *iter == ')') ||
       (*previous_iter == '[' && *iter == ']') || (*previous_iter == '<' && *iter == '>')) {
      get_source_buffer()->set_highlight_matching_brackets(false);
      guard.on_exit = [this] {
        get_source_buffer()->set_highlight_matching_brackets(true);
      };
    }

    auto condition_iter = previous_iter;
    condition_iter = find_non_whitespace_code_iter_backward(condition_iter);
    auto start_iter = get_start_of_expression(condition_iter);
    auto tabs_end_iter = get_tabs_end_iter(start_iter);
    auto tabs = get_line_before(tabs_end_iter);

    /*
     * Change tabs after ending comment block with an extra space (as in this case)
     */
    if(tabs.size() % tab_size == 1 && !start_iter.ends_line() && !is_code_iter(start_iter)) {
      auto end_of_line_iter = start_iter;
      end_of_line_iter.forward_to_line_end();
      auto line = get_buffer()->get_text(tabs_end_iter, end_of_line_iter);
      if(!line.empty() && line.raw().compare(0, 2, "*/") == 0) {
        tabs.pop_back();
        get_buffer()->insert_at_cursor('\n' + tabs);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }

    // Special indentation of {, [ and ( for for instance JavaScript, JSON, Rust
    if(use_fixed_continuation_indenting) {
      unsigned int open_symbol = 0, close_symbol = 0;
      if(*condition_iter == '[') {
        open_symbol = '[';
        close_symbol = ']';
      }
      else if(*condition_iter == '(') {
        open_symbol = '(';
        close_symbol = ')';
      }
      else if(*condition_iter == '{') {
        open_symbol = '{';
        close_symbol = '}';
      }
      if(open_symbol != 0 && is_code_iter(condition_iter)) {
        Gtk::TextIter found_iter;
        // Check if an ), ] or } is needed
        bool has_right_bracket = false;
        if(find_close_symbol_forward(iter, found_iter, open_symbol, close_symbol)) {
          auto found_tabs_end_iter = get_tabs_end_iter(found_iter);
          if(found_tabs_end_iter.get_line_offset() == tabs_end_iter.get_line_offset())
            has_right_bracket = true;
        }

        if(*iter == close_symbol) {
          get_buffer()->insert_at_cursor('\n' + tabs + tab + '\n' + tabs);
          auto insert_it = get_buffer()->get_insert()->get_iter();
          if(insert_it.backward_chars(tabs.size() + 1)) {
            scroll_to(get_buffer()->get_insert());
            get_buffer()->place_cursor(insert_it);
          }
          return true;
        }
        else if(!has_right_bracket) {
          // If line does not end with: (,[, or {, move contents after the left bracket to next line inside brackets
          if(!iter.ends_line() && *iter != ')' && *iter != ']' && *iter != '}') {
            get_buffer()->insert_at_cursor('\n' + tabs + tab);
            auto iter = get_buffer()->get_insert()->get_iter();
            auto mark = get_buffer()->create_mark(iter);
            iter.forward_to_line_end();
            get_buffer()->insert(iter, '\n' + tabs + static_cast<char>(close_symbol));
            scroll_to(get_buffer()->get_insert());
            get_buffer()->place_cursor(mark->get_iter());
            get_buffer()->delete_mark(mark);
            return true;
          }
          else {
            //Insert new lines with bracket end
            get_buffer()->insert_at_cursor('\n' + tabs + tab + '\n' + tabs + static_cast<char>(close_symbol));
            auto insert_it = get_buffer()->get_insert()->get_iter();
            if(insert_it.backward_chars(tabs.size() + 2)) {
              scroll_to(get_buffer()->get_insert());
              get_buffer()->place_cursor(insert_it);
            }
            return true;
          }
        }
        else {
          get_buffer()->insert_at_cursor('\n' + tabs + tab);
          scroll_to(get_buffer()->get_insert());
          return true;
        }
      }

      // JavaScript: simplified indentations inside brackets, after for example:
      // [\n  1, 2, 3,\n
      // return (\n
      // ReactDOM.render(\n  <div>\n
      Gtk::TextIter found_iter;
      auto after_start_iter = start_iter;
      after_start_iter.forward_char();
      if((*start_iter == '[' && (!find_close_symbol_forward(after_start_iter, found_iter, '[', ']') || found_iter > iter)) ||
         (*start_iter == '(' && (!find_close_symbol_forward(after_start_iter, found_iter, '(', ')') || found_iter > iter)) ||
         (*start_iter == '{' && (!find_close_symbol_forward(after_start_iter, found_iter, '{', '}') || found_iter > iter))) {
        get_buffer()->insert_at_cursor('\n' + tabs + tab);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    else {
      if(*condition_iter == '{' && is_code_iter(condition_iter)) {
        Gtk::TextIter close_iter;
        // Check if an '}' is needed
        bool has_right_curly_bracket = false;
        if(find_close_symbol_forward(iter, close_iter, '{', '}')) {
          auto found_tabs_end_iter = get_tabs_end_iter(close_iter);
          if(found_tabs_end_iter.get_line_offset() == tabs_end_iter.get_line_offset()) {
            has_right_curly_bracket = true;
            // Special case for functions and classes with no indentation after: namespace {
            if(is_cpp && tabs_end_iter.starts_line()) {
              auto iter = condition_iter;
              Gtk::TextIter open_iter;
              if(iter.backward_char() && find_open_curly_bracket_backward(iter, open_iter)) {
                if(open_iter.starts_line()) // in case of: namespace test\n{
                  open_iter.backward_char();
                auto iter = get_buffer()->get_iter_at_line(open_iter.get_line());
                if(get_token(iter) == "namespace")
                  has_right_curly_bracket = close_iter.forward_char() && find_close_symbol_forward(close_iter, close_iter, '{', '}');
              }
            }
          }
        }

        // Check if one should add semicolon after '}'
        bool add_semicolon = false;
        if(is_cpp) {
          // add semicolon after class or struct?
          auto token = get_token(tabs_end_iter);
          if(token == "class" || token == "struct")
            add_semicolon = true;
          // Add semicolon after lambda unless it's a parameter
          else if(*start_iter != '(' && *start_iter != '{' && *start_iter != '[') {
            auto it = condition_iter;
            long para_count = 0;
            long square_count = 0;
            bool square_outside_para_found = false;
            while(it.backward_char()) {
              if(*it == ']' && is_code_iter(it)) {
                --square_count;
                if(para_count == 0)
                  square_outside_para_found = true;
              }
              else if(*it == '[' && is_code_iter(it))
                ++square_count;
              else if(*it == ')' && is_code_iter(it))
                --para_count;
              else if(*it == '(' && is_code_iter(it))
                ++para_count;

              if(square_outside_para_found && square_count == 0 && para_count == 0) {
                add_semicolon = true;
                break;
              }
              if(it == start_iter)
                break;
              if(!square_outside_para_found && square_count == 0 && para_count == 0) {
                if((*it >= 'A' && *it <= 'Z') || (*it >= 'a' && *it <= 'z') || (*it >= '0' && *it <= '9') || *it == '_' ||
                   *it == '-' || *it == ' ' || *it == '\t' || *it == '<' || *it == '>' || *it == '(' || *it == ':' ||
                   *it == '*' || *it == '&' || *it == '/' || it.ends_line() || !is_code_iter(it)) {
                  continue;
                }
                else
                  break;
              }
            }
          }
        }

        if(*iter == '}') {
          get_buffer()->insert_at_cursor('\n' + tabs + tab + '\n' + tabs);
          if(add_semicolon) {
            // Check if semicolon exists
            auto next_iter = get_buffer()->get_insert()->get_iter();
            next_iter.forward_char();
            if(*next_iter != ';')
              get_buffer()->insert(next_iter, ";");
          }
          auto insert_it = get_buffer()->get_insert()->get_iter();
          if(insert_it.backward_chars(tabs.size() + 1)) {
            scroll_to(get_buffer()->get_insert());
            get_buffer()->place_cursor(insert_it);
          }
          return true;
        }
        else if(!has_right_curly_bracket) {
          // If line does not end with: {, move contents after { to next line inside brackets
          if(!iter.ends_line() && *iter != ')' && *iter != ']') {
            get_buffer()->insert_at_cursor('\n' + tabs + tab);
            auto iter = get_buffer()->get_insert()->get_iter();
            auto mark = get_buffer()->create_mark(iter);
            iter.forward_to_line_end();
            get_buffer()->insert(iter, '\n' + tabs + '}');
            scroll_to(get_buffer()->get_insert());
            get_buffer()->place_cursor(mark->get_iter());
            get_buffer()->delete_mark(mark);
            return true;
          }
          else {
            get_buffer()->insert_at_cursor('\n' + tabs + tab + '\n' + tabs + (add_semicolon ? "};" : "}"));
            auto insert_it = get_buffer()->get_insert()->get_iter();
            if(insert_it.backward_chars(tabs.size() + (add_semicolon ? 3 : 2))) {
              scroll_to(get_buffer()->get_insert());
              get_buffer()->place_cursor(insert_it);
            }
            return true;
          }
        }
        else {
          get_buffer()->insert_at_cursor('\n' + tabs + tab);
          scroll_to(get_buffer()->get_insert());
          return true;
        }
      }

      // Indent multiline expressions
      if(*start_iter == '(' || *start_iter == '[') {
        auto iter = get_tabs_end_iter(start_iter);
        auto tabs = get_line_before(iter);
        while(iter <= start_iter) {
          tabs += ' ';
          iter.forward_char();
        }
        get_buffer()->insert_at_cursor('\n' + tabs);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }

    auto after_condition_iter = condition_iter;
    after_condition_iter.forward_char();
    std::string sentence = get_buffer()->get_text(start_iter, after_condition_iter);
    std::smatch sm;
    // Indenting after for instance: if(...)\n
    if(std::regex_match(sentence, sm, no_bracket_statement_regex)) {
      get_buffer()->insert_at_cursor('\n' + tabs + tab);
      scroll_to(get_buffer()->get_insert());
      return true;
    }
    // Indenting after for instance: if(...)\n...;\n
    else if(*condition_iter == ';' && condition_iter.get_line() > 0 && is_code_iter(condition_iter)) {
      auto previous_end_iter = start_iter;
      while(previous_end_iter.backward_char() && !previous_end_iter.ends_line()) {
      }
      previous_end_iter = find_non_whitespace_code_iter_backward(previous_end_iter);
      auto previous_start_iter = get_tabs_end_iter(get_buffer()->get_iter_at_line(get_start_of_expression(previous_end_iter).get_line()));
      auto previous_tabs = get_line_before(previous_start_iter);
      if(!previous_end_iter.ends_line())
        previous_end_iter.forward_char();
      std::string previous_sentence = get_buffer()->get_text(previous_start_iter, previous_end_iter);
      std::smatch sm2;
      if(std::regex_match(previous_sentence, sm2, no_bracket_statement_regex)) {
        get_buffer()->insert_at_cursor('\n' + previous_tabs);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    // Indenting after ':'
    else if(*condition_iter == ':' && is_code_iter(condition_iter)) {
      bool perform_indent = true;
      auto iter = condition_iter;
      if(!iter.starts_line())
        iter.backward_char();
      while(!iter.starts_line() && *iter == ' ' && iter.backward_char()) {
      }
      if(*iter == ')') {
        auto token = get_token(get_tabs_end_iter(get_buffer()->get_iter_at_line(iter.get_line())));
        if(token != "case") // Do not move left for instance: void Test::Test():
          perform_indent = false;
      }

      if(perform_indent) {
        Gtk::TextIter found_curly_iter;
        if(find_open_curly_bracket_backward(iter, found_curly_iter)) {
          auto tabs_end_iter = get_tabs_end_iter(get_buffer()->get_iter_at_line(found_curly_iter.get_line()));
          auto tabs_start_of_sentence = get_line_before(tabs_end_iter);
          if(tabs.size() == (tabs_start_of_sentence.size() + tab_size)) {
            auto start_line_iter = get_buffer()->get_iter_at_line(iter.get_line());
            auto start_line_plus_tab_size = start_line_iter;
            for(size_t c = 0; c < tab_size; c++)
              start_line_plus_tab_size.forward_char();
            get_buffer()->erase(start_line_iter, start_line_plus_tab_size);
            get_buffer()->insert_at_cursor('\n' + tabs);
            scroll_to(get_buffer()->get_insert());
            return true;
          }
          else {
            get_buffer()->insert_at_cursor('\n' + tabs + tab);
            scroll_to(get_buffer()->get_insert());
            return true;
          }
        }
      }
    }
    // Indent as in current or next line
    int line_nr = iter.get_line();
    if(iter.ends_line() && (line_nr + 1) < get_buffer()->get_line_count()) {
      auto next_tabs_end_iter = get_tabs_end_iter(line_nr + 1);
      if(next_tabs_end_iter.get_line_offset() > tabs_end_iter.get_line_offset()) {
        get_buffer()->insert_at_cursor('\n' + get_line_before(next_tabs_end_iter));
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    get_buffer()->insert_at_cursor('\n' + tabs);
    scroll_to(get_buffer()->get_insert());
    return true;
  }
  // Indent left when writing }, ) or ] on a new line
  else if(key->keyval == GDK_KEY_braceright ||
          (use_fixed_continuation_indenting && (key->keyval == GDK_KEY_bracketright || key->keyval == GDK_KEY_parenright))) {
    std::string bracket;
    if(key->keyval == GDK_KEY_braceright)
      bracket = "}";
    if(key->keyval == GDK_KEY_bracketright)
      bracket = "]";
    else if(key->keyval == GDK_KEY_parenright)
      bracket = ")";
    std::string line = get_line_before();
    if(line.size() >= tab_size && iter.ends_line()) {
      bool indent_left = true;
      for(auto c : line) {
        if(c != tab_char) {
          indent_left = false;
          break;
        }
      }
      if(indent_left) {
        auto line_it = get_buffer()->get_iter_at_line(iter.get_line());
        auto line_plus_it = line_it;
        line_plus_it.forward_chars(tab_size);
        get_buffer()->erase(line_it, line_plus_it);
        get_buffer()->insert_at_cursor(bracket);
        return true;
      }
    }
  }
  // Indent left when writing { on a new line after for instance if(...)\n...
  else if(key->keyval == GDK_KEY_braceleft) {
    auto tabs_end_iter = get_tabs_end_iter();
    auto tabs = get_line_before(tabs_end_iter);
    size_t line_nr = iter.get_line();
    if(line_nr > 0 && tabs.size() >= tab_size && iter == tabs_end_iter) {
      auto previous_end_iter = iter;
      while(previous_end_iter.backward_char() && !previous_end_iter.ends_line()) {
      }
      auto condition_iter = find_non_whitespace_code_iter_backward(previous_end_iter);
      auto previous_start_iter = get_tabs_end_iter(get_buffer()->get_iter_at_line(get_start_of_expression(condition_iter).get_line()));
      auto previous_tabs = get_line_before(previous_start_iter);
      auto after_condition_iter = condition_iter;
      after_condition_iter.forward_char();
      if((tabs.size() - tab_size) == previous_tabs.size()) {
        std::string previous_sentence = get_buffer()->get_text(previous_start_iter, after_condition_iter);
        std::smatch sm;
        if(std::regex_match(previous_sentence, sm, no_bracket_statement_regex)) {
          auto start_iter = iter;
          start_iter.backward_chars(tab_size);
          get_buffer()->erase(start_iter, iter);
          get_buffer()->insert_at_cursor("{");
          scroll_to(get_buffer()->get_insert());
          return true;
        }
      }
    }
  }
  // Mark parameters of templated functions after pressing tab and after writing template argument
  else if(key->keyval == GDK_KEY_Tab && (key->state & GDK_SHIFT_MASK) == 0) {
    if(*iter == '>') {
      iter.forward_char();
      Gtk::TextIter parenthesis_end_iter;
      if(*iter == '(' && is_templated_function(iter, parenthesis_end_iter)) {
        iter.forward_char();
        get_buffer()->select_range(iter, parenthesis_end_iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    // Special case if insert is at beginning of empty line:
    else if(iter.starts_line() && iter.ends_line() && !get_buffer()->get_has_selection()) {
      // Indenting after for instance: if(...)\n...;\n
      auto condition_iter = iter;
      while(condition_iter.starts_line() && condition_iter.backward_char()) {
      }
      condition_iter = find_non_whitespace_code_iter_backward(condition_iter);
      if(*condition_iter == ';' && condition_iter.get_line() > 0 && is_code_iter(condition_iter)) {
        auto start_iter = get_start_of_expression(condition_iter);
        auto previous_end_iter = start_iter;
        while(previous_end_iter.backward_char() && !previous_end_iter.ends_line()) {
        }
        previous_end_iter = find_non_whitespace_code_iter_backward(previous_end_iter);
        auto previous_start_iter = get_tabs_end_iter(get_buffer()->get_iter_at_line(get_start_of_expression(previous_end_iter).get_line()));
        auto previous_tabs = get_line_before(previous_start_iter);
        if(!previous_end_iter.ends_line())
          previous_end_iter.forward_char();
        std::string previous_sentence = get_buffer()->get_text(previous_start_iter, previous_end_iter);
        std::smatch sm2;
        if(std::regex_match(previous_sentence, sm2, no_bracket_statement_regex)) {
          get_buffer()->insert_at_cursor(previous_tabs);
          scroll_to(get_buffer()->get_insert());
          return true;
        }
      }
    }
  }

  return false;
}

bool Source::View::on_key_press_event_smart_brackets(GdkEventKey *key) {
  if(get_buffer()->get_has_selection())
    return false;

  auto iter = get_buffer()->get_insert()->get_iter();
  auto previous_iter = iter;
  previous_iter.backward_char();
  if(is_code_iter(iter)) {
    //Move after ')' if closed expression
    if(key->keyval == GDK_KEY_parenright) {
      if(*iter == ')' && symbol_count(iter, '(', ')') <= 0) {
        iter.forward_char();
        get_buffer()->place_cursor(iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    //Move after '>' if >( and closed expression
    else if(key->keyval == GDK_KEY_greater) {
      if(*iter == '>') {
        iter.forward_char();
        Gtk::TextIter parenthesis_end_iter;
        if(*iter == '(' && is_templated_function(iter, parenthesis_end_iter)) {
          get_buffer()->place_cursor(iter);
          scroll_to(get_buffer()->get_insert());
          return true;
        }
      }
    }
    //Move after '(' if >( and select text inside parentheses
    else if(key->keyval == GDK_KEY_parenleft) {
      auto previous_iter = iter;
      previous_iter.backward_char();
      if(*previous_iter == '>') {
        Gtk::TextIter parenthesis_end_iter;
        if(*iter == '(' && is_templated_function(iter, parenthesis_end_iter)) {
          iter.forward_char();
          get_buffer()->select_range(iter, parenthesis_end_iter);
          scroll_to(iter);
          return true;
        }
      }
    }
  }

  return false;
}

bool Source::View::on_key_press_event_smart_inserts(GdkEventKey *key) {
  keep_snippet_marks = true;
  ScopeGuard guard{[this] {
    keep_snippet_marks = false;
  }};

  if(get_buffer()->get_has_selection()) {
    bool perform_insertion = false;
    char left_char, right_char;
    // Insert () around selection
    if(key->keyval == GDK_KEY_parenleft) {
      perform_insertion = true;
      left_char = '(';
      right_char = ')';
    }
    // Insert [] around selection
    else if(key->keyval == GDK_KEY_bracketleft) {
      perform_insertion = true;
      left_char = '[';
      right_char = ']';
    }
    // Insert {} around selection
    else if(key->keyval == GDK_KEY_braceleft) {
      perform_insertion = true;
      left_char = '{';
      right_char = '}';
    }
    // Insert <> around selection
    else if(key->keyval == GDK_KEY_less) {
      perform_insertion = true;
      left_char = '<';
      right_char = '>';
    }
    // Insert '' around selection
    else if(key->keyval == GDK_KEY_apostrophe) {
      perform_insertion = true;
      left_char = '\'';
      right_char = '\'';
    }
    // Insert "" around selection
    else if(key->keyval == GDK_KEY_quotedbl) {
      perform_insertion = true;
      left_char = '"';
      right_char = '"';
    }
    else if(language && language->get_id() == "markdown") {
      if(key->keyval == GDK_KEY_dead_grave) {
        perform_insertion = true;
        left_char = '`';
        right_char = '`';
      }
      if(key->keyval == GDK_KEY_asterisk) {
        perform_insertion = true;
        left_char = '*';
        right_char = '*';
      }
      if(key->keyval == GDK_KEY_underscore) {
        perform_insertion = true;
        left_char = '_';
        right_char = '_';
      }
      if(key->keyval == GDK_KEY_dead_tilde) {
        perform_insertion = true;
        left_char = '~';
        right_char = '~';
      }
    }
    if(perform_insertion) {
      Gtk::TextIter start, end;
      get_buffer()->get_selection_bounds(start, end);
      auto start_mark = get_buffer()->create_mark(start);
      auto end_mark = get_buffer()->create_mark(end);
      get_buffer()->insert(start, Glib::ustring() + left_char);
      get_buffer()->insert(end_mark->get_iter(), Glib::ustring() + right_char);
      auto start_mark_next_iter = start_mark->get_iter();
      start_mark_next_iter.forward_char();
      get_buffer()->select_range(start_mark_next_iter, end_mark->get_iter());
      get_buffer()->delete_mark(start_mark);
      get_buffer()->delete_mark(end_mark);
      return true;
    }
    return false;
  }

  auto iter = get_buffer()->get_insert()->get_iter();
  auto previous_iter = iter;
  previous_iter.backward_char();
  auto next_iter = iter;
  next_iter.forward_char();

  auto allow_insertion = [](const Gtk::TextIter &iter) {
    if(iter.ends_line() || *iter == ' ' || *iter == '\t' || *iter == ';' || *iter == ',' ||
       *iter == ')' || *iter == '[' || *iter == ']' || *iter == '{' || *iter == '}' || *iter == '<' || *iter == '>' || *iter == '/')
      return true;
    return false;
  };

  if(is_code_iter(iter)) {
    // Insert ()
    if(key->keyval == GDK_KEY_parenleft && allow_insertion(iter)) {
      if(symbol_count(iter, '(', ')') >= 0) {
        get_buffer()->insert_at_cursor(")");
        auto iter = get_buffer()->get_insert()->get_iter();
        iter.backward_char();
        get_buffer()->place_cursor(iter);
        scroll_to(get_buffer()->get_insert());
        return false;
      }
    }
    // Insert []
    else if(key->keyval == GDK_KEY_bracketleft && allow_insertion(iter)) {
      if(symbol_count(iter, '[', ']') >= 0) {
        get_buffer()->insert_at_cursor("]");
        auto iter = get_buffer()->get_insert()->get_iter();
        iter.backward_char();
        get_buffer()->place_cursor(iter);
        scroll_to(get_buffer()->get_insert());
        return false;
      }
    }
    // Move right on ] in []
    else if(key->keyval == GDK_KEY_bracketright) {
      if(*iter == ']' && symbol_count(iter, '[', ']') <= 0) {
        iter.forward_char();
        get_buffer()->place_cursor(iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    // Insert {}
    else if(key->keyval == GDK_KEY_braceleft && allow_insertion(iter)) {
      auto start_iter = get_start_of_expression(iter);
      // Do not add } if { is at end of line and next line has a higher indentation
      auto test_iter = iter;
      while(!test_iter.ends_line() && (*test_iter == ' ' || *test_iter == '\t' || !is_code_iter(test_iter)) && test_iter.forward_char()) {
      }
      if(test_iter.ends_line()) {
        if(iter.get_line() + 1 < get_buffer()->get_line_count() && *start_iter != '(' && *start_iter != '[' && *start_iter != '{') {
          auto tabs_end_iter = (get_tabs_end_iter(get_buffer()->get_iter_at_line(start_iter.get_line())));
          auto next_line_iter = get_buffer()->get_iter_at_line(iter.get_line() + 1);
          auto next_line_tabs_end_iter = (get_tabs_end_iter(get_buffer()->get_iter_at_line(next_line_iter.get_line())));
          if(next_line_tabs_end_iter.get_line_offset() > tabs_end_iter.get_line_offset())
            return false;
        }
      }

      Gtk::TextIter close_iter;
      bool has_right_curly_bracket = false;
      auto tabs_end_iter = get_tabs_end_iter(start_iter);
      if(find_close_symbol_forward(iter, close_iter, '{', '}')) {
        auto found_tabs_end_iter = get_tabs_end_iter(close_iter);
        if(found_tabs_end_iter.get_line_offset() == tabs_end_iter.get_line_offset()) {
          has_right_curly_bracket = true;
          // Special case for functions and classes with no indentation after: namespace {:
          if(is_cpp && tabs_end_iter.starts_line()) {
            Gtk::TextIter open_iter;
            if(find_open_curly_bracket_backward(iter, open_iter)) {
              if(open_iter.starts_line()) // in case of: namespace test\n{
                open_iter.backward_char();
              auto iter = get_buffer()->get_iter_at_line(open_iter.get_line());
              if(get_token(iter) == "namespace")
                has_right_curly_bracket = close_iter.forward_char() && find_close_symbol_forward(close_iter, close_iter, '{', '}');
            }
          }
          // Inside for example {}:
          else if(found_tabs_end_iter.get_line() == tabs_end_iter.get_line())
            has_right_curly_bracket = symbol_count(iter, '{', '}') < 0;
        }
      }
      if(!has_right_curly_bracket) {
        get_buffer()->insert_at_cursor("}");
        auto iter = get_buffer()->get_insert()->get_iter();
        iter.backward_char();
        get_buffer()->place_cursor(iter);
        scroll_to(get_buffer()->get_insert());
      }
      return false;
    }
    // Move right on } in {}
    else if(key->keyval == GDK_KEY_braceright) {
      if(*iter == '}' && symbol_count(iter, '{', '}') <= 0) {
        iter.forward_char();
        get_buffer()->place_cursor(iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
    }
    // Insert ''
    else if(key->keyval == GDK_KEY_apostrophe && allow_insertion(iter) && symbol_count(iter, '\'', -1) % 2 == 0) {
      get_buffer()->insert_at_cursor("''");
      auto iter = get_buffer()->get_insert()->get_iter();
      iter.backward_char();
      get_buffer()->place_cursor(iter);
      scroll_to(get_buffer()->get_insert());
      return true;
    }
    // Insert ""
    else if(key->keyval == GDK_KEY_quotedbl && allow_insertion(iter) && symbol_count(iter, '"', -1) % 2 == 0) {
      get_buffer()->insert_at_cursor("\"\"");
      auto iter = get_buffer()->get_insert()->get_iter();
      iter.backward_char();
      get_buffer()->place_cursor(iter);
      scroll_to(get_buffer()->get_insert());
      return true;
    }
    // Move right on last ' in '', or last " in ""
    else if(((key->keyval == GDK_KEY_apostrophe && *iter == '\'') || (key->keyval == GDK_KEY_quotedbl && *iter == '\"')) && is_spellcheck_iter(iter)) {
      get_buffer()->place_cursor(next_iter);
      scroll_to(get_buffer()->get_insert());
      return true;
    }
    // Insert ; at the end of line, if iter is at the last )
    else if(key->keyval == GDK_KEY_semicolon) {
      if(*iter == ')' && symbol_count(iter, '(', ')') <= 0 && next_iter.ends_line()) {
        auto start_iter = get_start_of_expression(previous_iter);
        if(*start_iter == '(') {
          start_iter.backward_char();
          if(*start_iter == ' ')
            start_iter.backward_char();
          auto token = get_token(start_iter);
          if(token != "for" && token != "if" && token != "while" && token != "else") {
            iter.forward_char();
            get_buffer()->place_cursor(iter);
            get_buffer()->insert_at_cursor(";");
            scroll_to(get_buffer()->get_insert());
            return true;
          }
        }
      }
    }
    // Delete ()
    else if(key->keyval == GDK_KEY_BackSpace) {
      if(*previous_iter == '(' && *iter == ')' && symbol_count(iter, '(', ')') <= 0) {
        auto next_iter = iter;
        next_iter.forward_char();
        get_buffer()->erase(previous_iter, next_iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
      // Delete []
      else if(*previous_iter == '[' && *iter == ']' && symbol_count(iter, '[', ']') <= 0) {
        auto next_iter = iter;
        next_iter.forward_char();
        get_buffer()->erase(previous_iter, next_iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
      // Delete {}
      else if(*previous_iter == '{' && *iter == '}' && symbol_count(iter, '{', '}') <= 0) {
        auto next_iter = iter;
        next_iter.forward_char();
        get_buffer()->erase(previous_iter, next_iter);
        scroll_to(get_buffer()->get_insert());
        return true;
      }
      // Delete '' or ""
      else if(key->keyval == GDK_KEY_BackSpace) {
        if((*previous_iter == '\'' && *iter == '\'') || (*previous_iter == '"' && *iter == '"')) {
          get_buffer()->erase(previous_iter, next_iter);
          scroll_to(get_buffer()->get_insert());
          return true;
        }
      }
    }
  }

  return false;
}

bool Source::View::on_key_press_event_extra_cursors(GdkEventKey *key) {
  setup_extra_cursor_signals();

  if(key->keyval == GDK_KEY_Escape && !extra_cursors.empty()) {
    for(auto &extra_cursor : extra_cursors) {
      extra_cursor.first->set_visible(false);
      get_buffer()->delete_mark(extra_cursor.first);
    }
    extra_cursors.clear();
    return true;
  }

  unsigned create_cursor_mask = GDK_MOD1_MASK;
  unsigned move_last_created_cursor_mask = GDK_SHIFT_MASK | GDK_MOD1_MASK;

  // Move last created cursor
  if((key->keyval == GDK_KEY_Left || key->keyval == GDK_KEY_KP_Left) && (key->state & move_last_created_cursor_mask) == move_last_created_cursor_mask) {
    if(extra_cursors.empty())
      return false;
    auto &cursor = extra_cursors.back().first;
    auto iter = cursor->get_iter();
    iter.backward_char();
    get_buffer()->move_mark(cursor, iter);
    return true;
  }
  if((key->keyval == GDK_KEY_Right || key->keyval == GDK_KEY_KP_Right) && (key->state & move_last_created_cursor_mask) == move_last_created_cursor_mask) {
    if(extra_cursors.empty())
      return false;
    auto &cursor = extra_cursors.back().first;
    auto iter = cursor->get_iter();
    iter.forward_char();
    get_buffer()->move_mark(cursor, iter);
    return true;
  }
  if((key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_KP_Up) && (key->state & move_last_created_cursor_mask) == move_last_created_cursor_mask) {
    if(extra_cursors.empty())
      return false;
    auto &extra_cursor = extra_cursors.back();
    auto iter = extra_cursor.first->get_iter();
    auto line_offset = extra_cursor.second;
    if(iter.backward_line()) {
      auto end_line_iter = iter;
      if(!end_line_iter.ends_line())
        end_line_iter.forward_to_line_end();
      iter.forward_chars(std::min(line_offset, end_line_iter.get_line_offset()));
      get_buffer()->move_mark(extra_cursor.first, iter);
    }
    return true;
  }
  if((key->keyval == GDK_KEY_Down || key->keyval == GDK_KEY_KP_Down) && (key->state & move_last_created_cursor_mask) == move_last_created_cursor_mask) {
    if(extra_cursors.empty())
      return false;
    auto &extra_cursor = extra_cursors.back();
    auto iter = extra_cursor.first->get_iter();
    auto line_offset = extra_cursor.second;
    if(iter.forward_line()) {
      auto end_line_iter = iter;
      if(!end_line_iter.ends_line())
        end_line_iter.forward_to_line_end();
      iter.forward_chars(std::min(line_offset, end_line_iter.get_line_offset()));
      get_buffer()->move_mark(extra_cursor.first, iter);
    }
    return true;
  }

  // Create extra cursor
  if((key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_KP_Up) && (key->state & create_cursor_mask) == create_cursor_mask) {
    auto insert_iter = get_buffer()->get_insert()->get_iter();
    auto insert_line_offset = insert_iter.get_line_offset();
    auto offset = insert_iter.get_offset();
    for(auto &extra_cursor : extra_cursors)
      offset = std::min(offset, extra_cursor.first->get_iter().get_offset());
    auto iter = get_buffer()->get_iter_at_offset(offset);
    if(iter.backward_line()) {
      auto end_line_iter = iter;
      if(!end_line_iter.ends_line())
        end_line_iter.forward_to_line_end();
      iter.forward_chars(std::min(insert_line_offset, end_line_iter.get_line_offset()));
      extra_cursors.emplace_back(get_buffer()->create_mark(iter, false), insert_line_offset);
      extra_cursors.back().first->set_visible(true);
    }
    return true;
  }
  if((key->keyval == GDK_KEY_Down || key->keyval == GDK_KEY_KP_Down) && (key->state & create_cursor_mask) == create_cursor_mask) {
    auto insert_iter = get_buffer()->get_insert()->get_iter();
    auto insert_line_offset = insert_iter.get_line_offset();
    auto offset = insert_iter.get_offset();
    for(auto &extra_cursor : extra_cursors)
      offset = std::max(offset, extra_cursor.first->get_iter().get_offset());
    auto iter = get_buffer()->get_iter_at_offset(offset);
    if(iter.forward_line()) {
      auto end_line_iter = iter;
      if(!end_line_iter.ends_line())
        end_line_iter.forward_to_line_end();
      iter.forward_chars(std::min(insert_line_offset, end_line_iter.get_line_offset()));
      extra_cursors.emplace_back(get_buffer()->create_mark(iter, false), insert_line_offset);
      extra_cursors.back().first->set_visible(true);
    }
    return true;
  }

  // Move cursors left/right
  if((key->keyval == GDK_KEY_Left || key->keyval == GDK_KEY_KP_Left) && (key->state & GDK_CONTROL_MASK) > 0) {
    enable_multiple_cursors = false;
    for(auto &extra_cursor : extra_cursors) {
      auto iter = extra_cursor.first->get_iter();
      iter.backward_word_start();
      extra_cursor.second = iter.get_line_offset();
      get_buffer()->move_mark(extra_cursor.first, iter);
    }
    auto insert = get_buffer()->get_insert();
    auto iter = insert->get_iter();
    iter.backward_word_start();
    get_buffer()->move_mark(insert, iter);
    if((key->state & GDK_SHIFT_MASK) == 0)
      get_buffer()->move_mark_by_name("selection_bound", iter);
    return true;
  }
  if((key->keyval == GDK_KEY_Right || key->keyval == GDK_KEY_KP_Right) && (key->state & GDK_CONTROL_MASK) > 0) {
    enable_multiple_cursors = false;
    for(auto &extra_cursor : extra_cursors) {
      auto iter = extra_cursor.first->get_iter();
      iter.forward_visible_word_end();
      extra_cursor.second = iter.get_line_offset();
      get_buffer()->move_mark(extra_cursor.first, iter);
    }
    auto insert = get_buffer()->get_insert();
    auto iter = insert->get_iter();
    iter.forward_visible_word_end();
    get_buffer()->move_mark(insert, iter);
    if((key->state & GDK_SHIFT_MASK) == 0)
      get_buffer()->move_mark_by_name("selection_bound", iter);
    return true;
  }

  // Move cursors up/down
  if((key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_KP_Up)) {
    enable_multiple_cursors = false;
    for(auto &extra_cursor : extra_cursors) {
      auto iter = extra_cursor.first->get_iter();
      auto line_offset = extra_cursor.second;
      if(iter.backward_line()) {
        auto end_line_iter = iter;
        if(!end_line_iter.ends_line())
          end_line_iter.forward_to_line_end();
        iter.forward_chars(std::min(line_offset, end_line_iter.get_line_offset()));
        get_buffer()->move_mark(extra_cursor.first, iter);
      }
    }
    return false;
  }
  if((key->keyval == GDK_KEY_Down || key->keyval == GDK_KEY_KP_Down)) {
    enable_multiple_cursors = false;
    for(auto &extra_cursor : extra_cursors) {
      auto iter = extra_cursor.first->get_iter();
      auto line_offset = extra_cursor.second;
      if(iter.forward_line()) {
        auto end_line_iter = iter;
        if(!end_line_iter.ends_line())
          end_line_iter.forward_to_line_end();
        iter.forward_chars(std::min(line_offset, end_line_iter.get_line_offset()));
        get_buffer()->move_mark(extra_cursor.first, iter);
      }
    }
    return false;
  }

  // Smart Home-key, start of line
  if((key->keyval == GDK_KEY_Home || key->keyval == GDK_KEY_KP_Home) && (key->state & GDK_CONTROL_MASK) == 0) {
    for(auto &extra_cursor : extra_cursors)
      get_buffer()->move_mark(extra_cursor.first, get_smart_home_iter(extra_cursor.first->get_iter()));
    enable_multiple_cursors = false;
    return false;
  }
  // Smart End-key, end of line
  if((key->keyval == GDK_KEY_End || key->keyval == GDK_KEY_KP_End) && (key->state & GDK_CONTROL_MASK) == 0) {
    for(auto &extra_cursor : extra_cursors)
      get_buffer()->move_mark(extra_cursor.first, get_smart_end_iter(extra_cursor.first->get_iter()));
    enable_multiple_cursors = false;
    return false;
  }

  return false;
}

bool Source::View::on_button_press_event(GdkEventButton *event) {
  // Select range when double clicking
  if(event->type == GDK_2BUTTON_PRESS) {
    Gtk::TextIter start, end;
    get_buffer()->get_selection_bounds(start, end);
    auto iter = start;
    while((*iter >= 48 && *iter <= 57) || (*iter >= 65 && *iter <= 90) || (*iter >= 97 && *iter <= 122) || *iter == 95) {
      start = iter;
      if(!iter.backward_char())
        break;
    }
    while((*end >= 48 && *end <= 57) || (*end >= 65 && *end <= 90) || (*end >= 97 && *end <= 122) || *end == 95) {
      if(!end.forward_char())
        break;
    }
    get_buffer()->select_range(start, end);
    return true;
  }

  // Go to implementation or declaration
  if((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
    if(event->state & primary_modifier_mask) {
      int x, y;
      window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, x, y);
      Gtk::TextIter iter;
      get_iter_at_location(iter, x, y);
      if(iter)
        get_buffer()->place_cursor(iter);

      Menu::get().actions["source_goto_declaration_or_implementation"]->activate();
      return true;
    }
  }

  // Open right click menu
  if((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
    hide_tooltips();
    if(!get_buffer()->get_has_selection()) {
      int x, y;
      window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, x, y);
      Gtk::TextIter iter;
      get_iter_at_location(iter, x, y);
      if(iter)
        get_buffer()->place_cursor(iter);
      Menu::get().right_click_line_menu->popup(event->button, event->time);
    }
    else {
      Menu::get().right_click_selected_menu->popup(event->button, event->time);
    }
    return true;
  }
  return Gsv::View::on_button_press_event(event);
}

bool Source::View::on_motion_notify_event(GdkEventMotion *event) {
  // Workaround for drag-and-drop crash on MacOS
  // TODO 2018: check if this bug has been fixed
#ifdef __APPLE__
  if((event->state & GDK_BUTTON1_MASK) == 0 || (event->state & GDK_SHIFT_MASK) > 0)
    return Gsv::View::on_motion_notify_event(event);
  else {
    int x, y;
    window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, x, y);
    Gtk::TextIter iter;
    get_iter_at_location(iter, x, y);
    get_buffer()->select_range(get_buffer()->get_insert()->get_iter(), iter);
    return true;
  }
#else
  return Gsv::View::on_motion_notify_event(event);
#endif
}
