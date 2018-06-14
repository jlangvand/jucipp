#include "source_base.h"
#include "config.h"
#include "git.h"
#include "info.h"
#include "terminal.h"
#include "utility.h"
#include <fstream>
#include <gtksourceview/gtksource.h>

Source::BaseView::BaseView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language) : Gsv::View(), file_path(file_path), language(language), status_diagnostics(0, 0, 0) {
  load(true);
  get_buffer()->place_cursor(get_buffer()->get_iter_at_offset(0));

  signal_focus_in_event().connect([this](GdkEventFocus *event) {
    if(this->last_write_time != static_cast<std::time_t>(-1))
      check_last_write_time();
    return false;
  });

  monitor_file();

  if(language) {
    get_source_buffer()->set_language(language);
    auto language_id = language->get_id();
    if(language_id == "chdr" || language_id == "cpphdr" || language_id == "c" ||
       language_id == "cpp" || language_id == "objc" || language_id == "java" ||
       language_id == "js" || language_id == "ts" || language_id == "proto" ||
       language_id == "c-sharp" || language_id == "html" || language_id == "cuda" ||
       language_id == "php" || language_id == "rust" || language_id == "swift" ||
       language_id == "go" || language_id == "scala" || language_id == "opencl" ||
       language_id == "json" || language_id == "css")
      is_bracket_language = true;
  }

#ifndef __APPLE__
  set_tab_width(4); //Visual size of a \t hardcoded to be equal to visual size of 4 spaces. Buggy on OS X
#endif
  tab_char = Config::get().source.default_tab_char;
  tab_size = Config::get().source.default_tab_size;
  if(Config::get().source.auto_tab_char_and_size) {
    auto tab_char_and_size = find_tab_char_and_size();
    if(tab_char_and_size.second != 0) {
      tab_char = tab_char_and_size.first;
      tab_size = tab_char_and_size.second;
    }
  }
  set_tab_char_and_size(tab_char, tab_size);

#ifdef __APPLE__
  primary_modifier_mask = GDK_MOD2_MASK;
#else
  primary_modifier_mask = GDK_CONTROL_MASK;
#endif


  search_settings = gtk_source_search_settings_new();
  gtk_source_search_settings_set_wrap_around(search_settings, true);
  search_context = gtk_source_search_context_new(get_source_buffer()->gobj(), search_settings);
  gtk_source_search_context_set_highlight(search_context, true);
  g_signal_connect(search_context, "notify::occurrences-count", G_CALLBACK(search_occurrences_updated), this);
}

Source::BaseView::~BaseView() {
  g_clear_object(&search_context);
  g_clear_object(&search_settings);

  monitor_changed_connection.disconnect();
  delayed_monitor_changed_connection.disconnect();
}

bool Source::BaseView::load(bool not_undoable_action) {
  boost::system::error_code ec;
  last_write_time = boost::filesystem::last_write_time(file_path, ec);
  if(ec)
    last_write_time = static_cast<std::time_t>(-1);

  disable_spellcheck = true;
  if(not_undoable_action)
    get_source_buffer()->begin_not_undoable_action();
  ScopeGuard guard{[this, not_undoable_action] {
    if(not_undoable_action)
      get_source_buffer()->end_not_undoable_action();
    disable_spellcheck = false;
  }};

  if(language) {
    std::ifstream input(file_path.string(), std::ofstream::binary);
    if(input) {
      std::stringstream ss;
      ss << input.rdbuf();
      Glib::ustring ustr = ss.str();

      bool valid = true;
      Glib::ustring::iterator iter;
      while(!ustr.validate(iter)) {
        auto next_char_iter = iter;
        next_char_iter++;
        ustr.replace(iter, next_char_iter, "?");
        valid = false;
      }

      if(!valid)
        Terminal::get().print("Warning: " + file_path.string() + " is not a valid UTF-8 file. Saving might corrupt the file.\n");

      if(get_buffer()->size() == 0)
        get_buffer()->insert_at_cursor(ustr);
      else
        replace_text(ustr.raw());
    }
    else
      return false;
  }
  else {
    std::ifstream input(file_path.string(), std::ofstream::binary);
    if(input) {
      std::stringstream ss;
      ss << input.rdbuf();
      Glib::ustring ustr = ss.str();

      if(ustr.validate()) {
        if(get_buffer()->size() == 0)
          get_buffer()->insert_at_cursor(ustr);
        else
          replace_text(ustr.raw());
      }
      else {
        Terminal::get().print("Error: " + file_path.string() + " is not a valid UTF-8 file.\n", true);
        return false;
      }
    }
    else
      return false;
  }

  get_buffer()->set_modified(false);
  return true;
}

void Source::BaseView::replace_text(const std::string &new_text) {
  get_buffer()->begin_user_action();

  if(get_buffer()->size() == 0) {
    get_buffer()->insert_at_cursor(new_text);
    get_buffer()->end_user_action();
    return;
  }
  else if(new_text.empty()) {
    get_buffer()->set_text(new_text);
    get_buffer()->end_user_action();
    return;
  }

  auto iter = get_buffer()->get_insert()->get_iter();
  int cursor_line_nr = iter.get_line();
  int cursor_line_offset = iter.ends_line() ? std::numeric_limits<int>::max() : iter.get_line_offset();

  std::vector<std::pair<const char *, const char *>> new_lines;

  const char *line_start = new_text.c_str();
  for(const char &chr : new_text) {
    if(chr == '\n') {
      new_lines.emplace_back(line_start, &chr + 1);
      line_start = &chr + 1;
    }
  }
  if(new_text.empty() || new_text.back() != '\n')
    new_lines.emplace_back(line_start, &new_text[new_text.size()]);

  try {
    auto hunks = Git::Repository::Diff::get_hunks(get_buffer()->get_text().raw(), new_text);

    for(auto it = hunks.rbegin(); it != hunks.rend(); ++it) {
      bool place_cursor = false;
      Gtk::TextIter start;
      if(it->old_lines.second != 0) {
        start = get_buffer()->get_iter_at_line(it->old_lines.first - 1);
        auto end = get_buffer()->get_iter_at_line(it->old_lines.first - 1 + it->old_lines.second);

        if(cursor_line_nr >= start.get_line() && cursor_line_nr < end.get_line()) {
          if(it->new_lines.second != 0) {
            place_cursor = true;
            int line_diff = cursor_line_nr - start.get_line();
            cursor_line_nr += static_cast<int>(0.5 + (static_cast<float>(line_diff) / it->old_lines.second) * it->new_lines.second) - line_diff;
          }
        }

        get_buffer()->erase(start, end);
        start = get_buffer()->get_iter_at_line(it->old_lines.first - 1);
      }
      else
        start = get_buffer()->get_iter_at_line(it->old_lines.first);
      if(it->new_lines.second != 0) {
        get_buffer()->insert(start, new_lines[it->new_lines.first - 1].first, new_lines[it->new_lines.first - 1 + it->new_lines.second - 1].second);
        if(place_cursor)
          place_cursor_at_line_offset(cursor_line_nr, cursor_line_offset);
      }
    }
  }
  catch(...) {
    Terminal::get().print("Error: Could not replace text in buffer\n", true);
  }

  get_buffer()->end_user_action();
}

void Source::BaseView::rename(const boost::filesystem::path &path) {
  file_path = path;

  boost::system::error_code ec;
  last_write_time = boost::filesystem::last_write_time(file_path, ec);
  if(ec)
    last_write_time = static_cast<std::time_t>(-1);
  monitor_file();

  if(update_status_file_path)
    update_status_file_path(this);
}

void Source::BaseView::monitor_file() {
#ifdef __APPLE__ // TODO: Gio file monitor is bugged on MacOS
  class Recursive {
  public:
    static void f(BaseView *view, std::time_t previous_last_write_time = static_cast<std::time_t>(-1), bool check_called = false) {
      view->delayed_monitor_changed_connection.disconnect();
      view->delayed_monitor_changed_connection = Glib::signal_timeout().connect([view, previous_last_write_time, check_called]() {
        boost::system::error_code ec;
        auto last_write_time = boost::filesystem::last_write_time(view->file_path, ec);
        if(!ec && last_write_time != view->last_write_time) {
          if(last_write_time == previous_last_write_time) { // If no change has happened in the last second (std::time_t is in seconds).
            if(!check_called)                               // To avoid several info messages when file is changed but not reloaded.
              view->check_last_write_time(last_write_time);
            Recursive::f(view, last_write_time, true);
            return false;
          }
          Recursive::f(view, last_write_time);
          return false;
        }
        Recursive::f(view);
        return false;
      }, 1000);
    }
  };
  delayed_monitor_changed_connection.disconnect();
  if(last_write_time != static_cast<std::time_t>(-1))
    Recursive::f(this);
#else
  if(this->last_write_time != static_cast<std::time_t>(-1)) {
    monitor = Gio::File::create_for_path(file_path.string())->monitor_file(Gio::FileMonitorFlags::FILE_MONITOR_NONE);
    monitor_changed_connection.disconnect();
    monitor_changed_connection = monitor->signal_changed().connect([this](const Glib::RefPtr<Gio::File> &file,
                                                                          const Glib::RefPtr<Gio::File> &,
                                                                          Gio::FileMonitorEvent monitor_event) {
      if(monitor_event != Gio::FileMonitorEvent::FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        delayed_monitor_changed_connection.disconnect();
        delayed_monitor_changed_connection = Glib::signal_timeout().connect([this]() {
          check_last_write_time();
          return false;
        }, 1000); // Has to wait 1 second (std::time_t is in seconds)
      }
    });
  }
#endif
}

void Source::BaseView::check_last_write_time(std::time_t last_write_time_) {
  if(this->last_write_time == static_cast<std::time_t>(-1))
    return;

  if(Config::get().source.auto_reload_changed_files && !get_buffer()->get_modified()) {
    boost::system::error_code ec;
    auto last_write_time = last_write_time_ != static_cast<std::time_t>(-1) ? last_write_time_ : boost::filesystem::last_write_time(file_path, ec);
    if(!ec && last_write_time != this->last_write_time) {
      if(load())
        return;
    }
  }
  else if(has_focus()) {
    boost::system::error_code ec;
    auto last_write_time = last_write_time_ != static_cast<std::time_t>(-1) ? last_write_time_ : boost::filesystem::last_write_time(file_path, ec);
    if(!ec && last_write_time != this->last_write_time)
      Info::get().print("Caution: " + file_path.filename().string() + " was changed outside of juCi++");
  }
}

std::pair<char, unsigned> Source::BaseView::find_tab_char_and_size() {
  if(language && language->get_id() == "python")
    return {' ', 4};

  std::map<char, size_t> tab_chars;
  std::map<unsigned, size_t> tab_sizes;
  auto iter = get_buffer()->begin();
  long tab_count = -1;
  long last_tab_count = 0;
  bool single_quoted = false;
  bool double_quoted = false;
  //For bracket languages, TODO: add more language ids
  if(is_bracket_language && !(language && language->get_id() == "html")) {
    bool line_comment = false;
    bool comment = false;
    bool bracket_last_line = false;
    char last_char = 0;
    long last_tab_diff = -1;
    while(iter) {
      if(iter.starts_line()) {
        line_comment = false;
        single_quoted = false;
        double_quoted = false;
        tab_count = 0;
        if(last_char == '{')
          bracket_last_line = true;
        else
          bracket_last_line = false;
      }
      if(bracket_last_line && tab_count != -1) {
        if(*iter == ' ') {
          tab_chars[' ']++;
          tab_count++;
        }
        else if(*iter == '\t') {
          tab_chars['\t']++;
          tab_count++;
        }
        else {
          auto line_iter = iter;
          char last_line_char = 0;
          while(line_iter && !line_iter.ends_line()) {
            if(*line_iter != ' ' && *line_iter != '\t')
              last_line_char = *line_iter;
            if(*line_iter == '(')
              break;
            line_iter.forward_char();
          }
          if(last_line_char == ':' || *iter == '#') {
            tab_count = 0;
            if((iter.get_line() + 1) < get_buffer()->get_line_count()) {
              iter = get_buffer()->get_iter_at_line(iter.get_line() + 1);
              continue;
            }
          }
          else if(!iter.ends_line()) {
            if(tab_count != last_tab_count)
              tab_sizes[std::abs(tab_count - last_tab_count)]++;
            last_tab_diff = std::abs(tab_count - last_tab_count);
            last_tab_count = tab_count;
            last_char = 0;
          }
        }
      }

      auto prev_iter = iter;
      prev_iter.backward_char();
      auto prev_prev_iter = prev_iter;
      prev_prev_iter.backward_char();
      if(!double_quoted && *iter == '\'' && !(*prev_iter == '\\' && *prev_prev_iter != '\\'))
        single_quoted = !single_quoted;
      else if(!single_quoted && *iter == '\"' && !(*prev_iter == '\\' && *prev_prev_iter != '\\'))
        double_quoted = !double_quoted;
      else if(!single_quoted && !double_quoted) {
        auto next_iter = iter;
        next_iter.forward_char();
        if(*iter == '/' && *next_iter == '/')
          line_comment = true;
        else if(*iter == '/' && *next_iter == '*')
          comment = true;
        else if(*iter == '*' && *next_iter == '/') {
          iter.forward_char();
          iter.forward_char();
          comment = false;
        }
      }
      if(!single_quoted && !double_quoted && !comment && !line_comment && *iter != ' ' && *iter != '\t' && !iter.ends_line())
        last_char = *iter;
      if(!single_quoted && !double_quoted && !comment && !line_comment && *iter == '}' && tab_count != -1 && last_tab_diff != -1)
        last_tab_count -= last_tab_diff;
      if(*iter != ' ' && *iter != '\t')
        tab_count = -1;

      iter.forward_char();
    }
  }
  else {
    long para_count = 0;
    while(iter) {
      if(iter.starts_line())
        tab_count = 0;
      if(tab_count != -1 && para_count == 0 && single_quoted == false && double_quoted == false) {
        if(*iter == ' ') {
          tab_chars[' ']++;
          tab_count++;
        }
        else if(*iter == '\t') {
          tab_chars['\t']++;
          tab_count++;
        }
        else if(!iter.ends_line()) {
          if(tab_count != last_tab_count)
            tab_sizes[std::abs(tab_count - last_tab_count)]++;
          last_tab_count = tab_count;
        }
      }
      auto prev_iter = iter;
      prev_iter.backward_char();
      auto prev_prev_iter = prev_iter;
      prev_prev_iter.backward_char();
      if(!double_quoted && *iter == '\'' && !(*prev_iter == '\\' && *prev_prev_iter != '\\'))
        single_quoted = !single_quoted;
      else if(!single_quoted && *iter == '\"' && !(*prev_iter == '\\' && *prev_prev_iter != '\\'))
        double_quoted = !double_quoted;
      else if(!single_quoted && !double_quoted) {
        if(*iter == '(')
          para_count++;
        else if(*iter == ')')
          para_count--;
      }
      if(*iter != ' ' && *iter != '\t')
        tab_count = -1;

      iter.forward_char();
    }
  }

  char found_tab_char = 0;
  size_t occurences = 0;
  for(auto &tab_char : tab_chars) {
    if(tab_char.second > occurences) {
      found_tab_char = tab_char.first;
      occurences = tab_char.second;
    }
  }
  unsigned found_tab_size = 0;
  occurences = 0;
  for(auto &tab_size : tab_sizes) {
    if(tab_size.second > occurences) {
      found_tab_size = tab_size.first;
      occurences = tab_size.second;
    }
  }
  return {found_tab_char, found_tab_size};
}

void Source::BaseView::set_tab_char_and_size(char tab_char_, unsigned tab_size_) {
  tab_char = tab_char_;
  tab_size = tab_size_;

  tab.clear();
  for(unsigned c = 0; c < tab_size; c++)
    tab += tab_char;
}

Gtk::TextIter Source::BaseView::get_iter_at_line_pos(int line, int pos) {
  return get_iter_at_line_index(line, pos);
}

Gtk::TextIter Source::BaseView::get_iter_at_line_offset(int line, int offset) {
  line = std::min(line, get_buffer()->get_line_count() - 1);
  if(line < 0)
    line = 0;
  auto iter = get_iter_at_line_end(line);
  offset = std::min(offset, iter.get_line_offset());
  if(offset < 0)
    offset = 0;
  return get_buffer()->get_iter_at_line_offset(line, offset);
}

Gtk::TextIter Source::BaseView::get_iter_at_line_index(int line, int index) {
  line = std::min(line, get_buffer()->get_line_count() - 1);
  if(line < 0)
    line = 0;
  auto iter = get_iter_at_line_end(line);
  index = std::min(index, iter.get_line_index());
  if(index < 0)
    index = 0;
  return get_buffer()->get_iter_at_line_index(line, index);
}

Gtk::TextIter Source::BaseView::get_iter_at_line_end(int line_nr) {
  if(line_nr >= get_buffer()->get_line_count())
    return get_buffer()->end();
  else if(line_nr + 1 < get_buffer()->get_line_count()) {
    auto iter = get_buffer()->get_iter_at_line(line_nr + 1);
    iter.backward_char();
    if(!iter.ends_line()) // for CR+LF
      iter.backward_char();
    return iter;
  }
  else {
    auto iter = get_buffer()->get_iter_at_line(line_nr);
    while(!iter.ends_line() && iter.forward_char()) {
    }
    return iter;
  }
}

Gtk::TextIter Source::BaseView::get_iter_for_dialog() {
  auto iter = get_buffer()->get_insert()->get_iter();
  Gdk::Rectangle visible_rect;
  get_visible_rect(visible_rect);
  Gdk::Rectangle iter_rect;
  get_iter_location(iter, iter_rect);
  iter_rect.set_width(1);
  if(iter.get_line_offset() >= 80) {
    get_iter_at_location(iter, visible_rect.get_x(), iter_rect.get_y());
    get_iter_location(iter, iter_rect);
  }
  if(!visible_rect.intersects(iter_rect))
    get_iter_at_location(iter, visible_rect.get_x(), visible_rect.get_y() + visible_rect.get_height() / 3);
  return iter;
}

void Source::BaseView::place_cursor_at_line_pos(int line, int pos) {
  get_buffer()->place_cursor(get_iter_at_line_pos(line, pos));
}

void Source::BaseView::place_cursor_at_line_offset(int line, int offset) {
  get_buffer()->place_cursor(get_iter_at_line_offset(line, offset));
}

void Source::BaseView::place_cursor_at_line_index(int line, int index) {
  get_buffer()->place_cursor(get_iter_at_line_index(line, index));
}

Gtk::TextIter Source::BaseView::get_smart_home_iter(const Gtk::TextIter &iter) {
  auto start_line_iter = get_buffer()->get_iter_at_line(iter.get_line());
  auto start_sentence_iter = start_line_iter;
  while(!start_sentence_iter.ends_line() &&
        (*start_sentence_iter == ' ' || *start_sentence_iter == '\t') &&
        start_sentence_iter.forward_char()) {
  }

  if(iter > start_sentence_iter || iter == start_line_iter)
    return start_sentence_iter;
  else
    return start_line_iter;
}

Gtk::TextIter Source::BaseView::get_smart_end_iter(const Gtk::TextIter &iter) {
  auto end_line_iter = get_iter_at_line_end(iter.get_line());
  auto end_sentence_iter = end_line_iter;
  while(!end_sentence_iter.starts_line() &&
        (*end_sentence_iter == ' ' || *end_sentence_iter == '\t' || end_sentence_iter.ends_line()) &&
        end_sentence_iter.backward_char()) {
  }
  if(!end_sentence_iter.ends_line() && *end_sentence_iter != ' ' && *end_sentence_iter != '\t')
    end_sentence_iter.forward_char();

  if(iter == end_line_iter)
    return end_sentence_iter;
  else
    return end_line_iter;
}

std::string Source::BaseView::get_line(const Gtk::TextIter &iter) {
  auto line_start_it = get_buffer()->get_iter_at_line(iter.get_line());
  auto line_end_it = get_iter_at_line_end(iter.get_line());
  std::string line(get_buffer()->get_text(line_start_it, line_end_it));
  return line;
}
std::string Source::BaseView::get_line(const Glib::RefPtr<Gtk::TextBuffer::Mark> &mark) {
  return get_line(mark->get_iter());
}
std::string Source::BaseView::get_line(int line_nr) {
  return get_line(get_buffer()->get_iter_at_line(line_nr));
}
std::string Source::BaseView::get_line() {
  return get_line(get_buffer()->get_insert());
}

std::string Source::BaseView::get_line_before(const Gtk::TextIter &iter) {
  auto line_it = get_buffer()->get_iter_at_line(iter.get_line());
  std::string line(get_buffer()->get_text(line_it, iter));
  return line;
}
std::string Source::BaseView::get_line_before(const Glib::RefPtr<Gtk::TextBuffer::Mark> &mark) {
  return get_line_before(mark->get_iter());
}
std::string Source::BaseView::get_line_before() {
  return get_line_before(get_buffer()->get_insert());
}

Gtk::TextIter Source::BaseView::get_tabs_end_iter(const Gtk::TextIter &iter) {
  return get_tabs_end_iter(iter.get_line());
}
Gtk::TextIter Source::BaseView::get_tabs_end_iter(const Glib::RefPtr<Gtk::TextBuffer::Mark> &mark) {
  return get_tabs_end_iter(mark->get_iter());
}
Gtk::TextIter Source::BaseView::get_tabs_end_iter(int line_nr) {
  auto sentence_iter = get_buffer()->get_iter_at_line(line_nr);
  while((*sentence_iter == ' ' || *sentence_iter == '\t') && !sentence_iter.ends_line() && sentence_iter.forward_char()) {
  }
  return sentence_iter;
}
Gtk::TextIter Source::BaseView::get_tabs_end_iter() {
  return get_tabs_end_iter(get_buffer()->get_insert());
}

std::pair<Gtk::TextIter, Gtk::TextIter> Source::BaseView::get_token_iters(Gtk::TextIter iter) {
  auto start = iter;
  auto end = iter;

  while((*iter >= 'A' && *iter <= 'Z') || (*iter >= 'a' && *iter <= 'z') || (*iter >= '0' && *iter <= '9') || *iter == '_') {
    start = iter;
    if(!iter.backward_char())
      break;
  }
  while((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') || (*end >= '0' && *end <= '9') || *end == '_') {
    if(!end.forward_char())
      break;
  }

  return {start, end};
}

std::string Source::BaseView::get_token(const Gtk::TextIter &iter) {
  auto range = get_token_iters(iter);
  return get_buffer()->get_text(range.first, range.second);
}

void Source::BaseView::cleanup_whitespace_characters() {
  auto buffer = get_buffer();
  buffer->begin_user_action();
  for(int line = 0; line < buffer->get_line_count(); line++) {
    auto iter = buffer->get_iter_at_line(line);
    auto end_iter = get_iter_at_line_end(line);
    if(iter == end_iter)
      continue;
    iter = end_iter;
    while(!iter.starts_line() && (*iter == ' ' || *iter == '\t' || iter.ends_line()))
      iter.backward_char();
    if(*iter != ' ' && *iter != '\t')
      iter.forward_char();
    if(iter == end_iter)
      continue;
    buffer->erase(iter, end_iter);
  }
  auto iter = buffer->end();
  if(!iter.starts_line())
    buffer->insert(buffer->end(), "\n");
  buffer->end_user_action();
}

void Source::BaseView::cleanup_whitespace_characters(const Gtk::TextIter &iter) {
  auto start_blank_iter = iter;
  auto end_blank_iter = iter;
  while((*end_blank_iter == ' ' || *end_blank_iter == '\t') &&
        !end_blank_iter.ends_line() && end_blank_iter.forward_char()) {
  }
  if(!start_blank_iter.starts_line()) {
    start_blank_iter.backward_char();
    while((*start_blank_iter == ' ' || *start_blank_iter == '\t') &&
          !start_blank_iter.starts_line() && start_blank_iter.backward_char()) {
    }
    if(*start_blank_iter != ' ' && *start_blank_iter != '\t')
      start_blank_iter.forward_char();
  }

  if(start_blank_iter.starts_line())
    get_buffer()->erase(iter, end_blank_iter);
  else
    get_buffer()->erase(start_blank_iter, end_blank_iter);
}

void Source::BaseView::paste() {
  enable_multiple_cursors = true;
  ScopeGuard guard{[this] {
    enable_multiple_cursors = false;
  }};

  std::string text = Gtk::Clipboard::get()->wait_for_text();

  //Replace carriage returns (which leads to crash) with newlines
  for(size_t c = 0; c < text.size(); c++) {
    if(text[c] == '\r') {
      if((c + 1) < text.size() && text[c + 1] == '\n')
        text.replace(c, 2, "\n");
      else
        text.replace(c, 1, "\n");
    }
  }

  //Exception for when pasted text is only whitespaces
  bool only_whitespaces = true;
  for(auto &chr : text) {
    if(chr != '\n' && chr != '\r' && chr != ' ' && chr != '\t') {
      only_whitespaces = false;
      break;
    }
  }
  if(only_whitespaces) {
    Gtk::Clipboard::get()->set_text(text);
    get_buffer()->paste_clipboard(Gtk::Clipboard::get());
    scroll_to_cursor_delayed(this, false, false);
    return;
  }

  get_buffer()->begin_user_action();
  if(get_buffer()->get_has_selection()) {
    Gtk::TextIter start, end;
    get_buffer()->get_selection_bounds(start, end);
    get_buffer()->erase(start, end);
  }
  auto iter = get_buffer()->get_insert()->get_iter();
  auto tabs_end_iter = get_tabs_end_iter();
  auto prefix_tabs = get_line_before(iter < tabs_end_iter ? iter : tabs_end_iter);

  size_t start_line = 0;
  size_t end_line = 0;
  bool paste_line = false;
  bool first_paste_line = true;
  size_t paste_line_tabs = -1;
  bool first_paste_line_has_tabs = false;
  for(size_t c = 0; c < text.size(); c++) {
    if(text[c] == '\n') {
      end_line = c;
      paste_line = true;
    }
    else if(c == text.size() - 1) {
      end_line = c + 1;
      paste_line = true;
    }
    if(paste_line) {
      bool empty_line = true;
      std::string line = text.substr(start_line, end_line - start_line);
      size_t tabs = 0;
      for(auto chr : line) {
        if(chr == tab_char)
          tabs++;
        else {
          empty_line = false;
          break;
        }
      }
      if(first_paste_line) {
        if(tabs != 0) {
          first_paste_line_has_tabs = true;
          paste_line_tabs = tabs;
        }
        else if(language && language->get_id() == "python") { // Special case for Python code where the first line ends with ':'
          char last_char = 0;
          for(auto &chr : line) {
            if(chr != ' ' && chr != '\t')
              last_char = chr;
          }
          if(last_char == ':') {
            first_paste_line_has_tabs = true;
            paste_line_tabs = tabs;
          }
        }
        first_paste_line = false;
      }
      else if(!empty_line)
        paste_line_tabs = std::min(paste_line_tabs, tabs);

      start_line = end_line + 1;
      paste_line = false;
    }
  }
  if(paste_line_tabs == static_cast<size_t>(-1))
    paste_line_tabs = 0;
  start_line = 0;
  end_line = 0;
  paste_line = false;
  first_paste_line = true;
  for(size_t c = 0; c < text.size(); c++) {
    if(text[c] == '\n') {
      end_line = c;
      paste_line = true;
    }
    else if(c == text.size() - 1) {
      end_line = c + 1;
      paste_line = true;
    }
    if(paste_line) {
      std::string line = text.substr(start_line, end_line - start_line);
      size_t line_tabs = 0;
      for(auto chr : line) {
        if(chr == tab_char)
          line_tabs++;
        else
          break;
      }
      auto tabs = paste_line_tabs;
      if(!(first_paste_line && !first_paste_line_has_tabs) && line_tabs < paste_line_tabs) {
        tabs = line_tabs;
      }

      if(first_paste_line) {
        if(first_paste_line_has_tabs)
          get_buffer()->insert_at_cursor(text.substr(start_line + tabs, end_line - start_line - tabs));
        else
          get_buffer()->insert_at_cursor(text.substr(start_line, end_line - start_line));
        first_paste_line = false;
      }
      else
        get_buffer()->insert_at_cursor('\n' + prefix_tabs + text.substr(start_line + tabs, end_line - start_line - tabs));
      start_line = end_line + 1;
      paste_line = false;
    }
  }
  // add final newline if present in text
  if(text.size() > 0 && text.back() == '\n')
    get_buffer()->insert_at_cursor('\n' + prefix_tabs);
  get_buffer()->place_cursor(get_buffer()->get_insert()->get_iter());
  get_buffer()->end_user_action();
  scroll_to_cursor_delayed(this, false, false);
}

std::string Source::BaseView::get_selected_text() {
  Gtk::TextIter start, end;
  if(!get_buffer()->get_selection_bounds(start, end)) {
    return {};
  }
  return get_buffer()->get_text(start, end);
}

void Source::BaseView::search_highlight(const std::string &text, bool case_sensitive, bool regex) {
  gtk_source_search_settings_set_case_sensitive(search_settings, case_sensitive);
  gtk_source_search_settings_set_regex_enabled(search_settings, regex);
  gtk_source_search_settings_set_search_text(search_settings, text.c_str());
  search_occurrences_updated(nullptr, nullptr, this);
}

void Source::BaseView::search_forward() {
  Gtk::TextIter insert, selection_bound;
  get_buffer()->get_selection_bounds(insert, selection_bound);
  auto &start = selection_bound;
  Gtk::TextIter match_start, match_end;
#if defined(GTK_SOURCE_MAJOR_VERSION) && (GTK_SOURCE_MAJOR_VERSION > 3 || (GTK_SOURCE_MAJOR_VERSION == 3 && GTK_SOURCE_MINOR_VERSION >= 22))
  gboolean has_wrapped_around;
  if(gtk_source_search_context_forward2(search_context, start.gobj(), match_start.gobj(), match_end.gobj(), &has_wrapped_around)) {
    get_buffer()->select_range(match_start, match_end);
    scroll_to(get_buffer()->get_insert());
  }
#else
  if(gtk_source_search_context_forward(search_context, start.gobj(), match_start.gobj(), match_end.gobj())) {
    get_buffer()->select_range(match_start, match_end);
    scroll_to(get_buffer()->get_insert());
  }
#endif
}

void Source::BaseView::search_backward() {
  Gtk::TextIter insert, selection_bound;
  get_buffer()->get_selection_bounds(insert, selection_bound);
  auto &start = insert;
  Gtk::TextIter match_start, match_end;
#if defined(GTK_SOURCE_MAJOR_VERSION) && (GTK_SOURCE_MAJOR_VERSION > 3 || (GTK_SOURCE_MAJOR_VERSION == 3 && GTK_SOURCE_MINOR_VERSION >= 22))
  gboolean has_wrapped_around;
  if(gtk_source_search_context_backward2(search_context, start.gobj(), match_start.gobj(), match_end.gobj(), &has_wrapped_around)) {
    get_buffer()->select_range(match_start, match_end);
    scroll_to(get_buffer()->get_insert());
  }
#else
  if(gtk_source_search_context_backward(search_context, start.gobj(), match_start.gobj(), match_end.gobj())) {
    get_buffer()->select_range(match_start, match_end);
    scroll_to(get_buffer()->get_insert());
  }
#endif
}

void Source::BaseView::replace_forward(const std::string &replacement) {
  Gtk::TextIter insert, selection_bound;
  get_buffer()->get_selection_bounds(insert, selection_bound);
  auto &start = insert;
  Gtk::TextIter match_start, match_end;
#if defined(GTK_SOURCE_MAJOR_VERSION) && (GTK_SOURCE_MAJOR_VERSION > 3 || (GTK_SOURCE_MAJOR_VERSION == 3 && GTK_SOURCE_MINOR_VERSION >= 22))
  gboolean has_wrapped_around;
  if(gtk_source_search_context_forward2(search_context, start.gobj(), match_start.gobj(), match_end.gobj(), &has_wrapped_around)) {
    auto offset = match_start.get_offset();
    gtk_source_search_context_replace2(search_context, match_start.gobj(), match_end.gobj(), replacement.c_str(), replacement.size(), nullptr);
    Glib::ustring replacement_ustring = replacement;
    get_buffer()->select_range(get_buffer()->get_iter_at_offset(offset), get_buffer()->get_iter_at_offset(offset + replacement_ustring.size()));
    scroll_to(get_buffer()->get_insert());
  }
#else
  if(gtk_source_search_context_forward(search_context, start.gobj(), match_start.gobj(), match_end.gobj())) {
    auto offset = match_start.get_offset();
    gtk_source_search_context_replace(search_context, match_start.gobj(), match_end.gobj(), replacement.c_str(), replacement.size(), nullptr);
    Glib::ustring replacement_ustring = replacement;
    get_buffer()->select_range(get_buffer()->get_iter_at_offset(offset), get_buffer()->get_iter_at_offset(offset + replacement_ustring.size()));
    scroll_to(get_buffer()->get_insert());
  }
#endif
}

void Source::BaseView::replace_backward(const std::string &replacement) {
  Gtk::TextIter insert, selection_bound;
  get_buffer()->get_selection_bounds(insert, selection_bound);
  auto &start = selection_bound;
  Gtk::TextIter match_start, match_end;
#if defined(GTK_SOURCE_MAJOR_VERSION) && (GTK_SOURCE_MAJOR_VERSION > 3 || (GTK_SOURCE_MAJOR_VERSION == 3 && GTK_SOURCE_MINOR_VERSION >= 22))
  gboolean has_wrapped_around;
  if(gtk_source_search_context_backward2(search_context, start.gobj(), match_start.gobj(), match_end.gobj(), &has_wrapped_around)) {
    auto offset = match_start.get_offset();
    gtk_source_search_context_replace2(search_context, match_start.gobj(), match_end.gobj(), replacement.c_str(), replacement.size(), nullptr);
    get_buffer()->select_range(get_buffer()->get_iter_at_offset(offset), get_buffer()->get_iter_at_offset(offset + replacement.size()));
    scroll_to(get_buffer()->get_insert());
  }
#else
  if(gtk_source_search_context_backward(search_context, start.gobj(), match_start.gobj(), match_end.gobj())) {
    auto offset = match_start.get_offset();
    gtk_source_search_context_replace(search_context, match_start.gobj(), match_end.gobj(), replacement.c_str(), replacement.size(), nullptr);
    get_buffer()->select_range(get_buffer()->get_iter_at_offset(offset), get_buffer()->get_iter_at_offset(offset + replacement.size()));
    scroll_to(get_buffer()->get_insert());
  }
#endif
}

void Source::BaseView::replace_all(const std::string &replacement) {
  gtk_source_search_context_replace_all(search_context, replacement.c_str(), replacement.size(), nullptr);
}

void Source::BaseView::search_occurrences_updated(GtkWidget *widget, GParamSpec *property, gpointer data) {
  auto view = static_cast<Source::BaseView *>(data);
  if(view->update_search_occurrences)
    view->update_search_occurrences(gtk_source_search_context_get_occurrences_count(view->search_context));
}
