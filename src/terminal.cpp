#include "terminal.hpp"
#include "config.hpp"
#include "filesystem.hpp"
#include "info.hpp"
#include "notebook.hpp"
#include "project.hpp"
#include "utility.hpp"
#include <iostream>
#include <regex>
#include <thread>

Terminal::Terminal() : Source::SearchView() {
  get_style_context()->add_class("juci_terminal");

  set_editable(false);

  bold_tag = get_buffer()->create_tag();
  bold_tag->property_weight() = Pango::WEIGHT_ULTRAHEAVY;

  link_tag = get_buffer()->create_tag();
  link_tag->property_underline() = Pango::Underline::UNDERLINE_SINGLE;

  link_mouse_cursor = Gdk::Cursor::create(Gdk::CursorType::HAND1);
  default_mouse_cursor = Gdk::Cursor::create(Gdk::CursorType::XTERM);

  // Apply link tags
  get_buffer()->signal_insert().connect([this](const Gtk::TextIter &iter, const Glib::ustring &text, int /*bytes*/) {
    std::string line_start;
    size_t line_start_pos = 0;
    bool delimiter_found = false;
    bool dot_found = false;
    bool number_found = false;

    auto start = iter;
    start.backward_chars(text.size());
    int line_nr = start.get_line();

    auto parse_text = [&](const std::string &text) {
      for(size_t i = 0; i < text.size(); ++i) {
        if(text[i] == '\n') {
          if(delimiter_found && dot_found && number_found) {
            if(auto link = !line_start.empty()
                               ? find_link(line_start + text.substr(0, i))
                               : find_link(text, line_start_pos, i - line_start_pos)) {
              auto link_start = get_buffer()->get_iter_at_line(line_nr);
              auto link_end = link_start;
              link_start.forward_chars(link->start_pos);
              link_end.forward_chars(link->end_pos);
              get_buffer()->apply_tag(link_tag, link_start, link_end);
            }
          }
          line_start_pos = i + 1;
          delimiter_found = false;
          dot_found = false;
          number_found = false;
          ++line_nr;
          line_start.clear();
        }
        else if(text[i] == '/' || text[i] == '\\')
          delimiter_found = true;
        else if(text[i] == '.')
          dot_found = true;
        else if(text[i] >= '0' && text[i] <= '9')
          number_found = true;
      }
    };

    if(!start.starts_line()) {
      auto end = start;
      start = get_buffer()->get_iter_at_line(start.get_line());
      line_start = get_buffer()->get_text(start, end).raw();
      parse_text(line_start);
    }
    parse_text(text.raw());
  });
}

int Terminal::process(const std::string &command, const boost::filesystem::path &path, bool use_pipes) {
  std::unique_ptr<TinyProcessLib::Process> process;
  if(use_pipes)
    process = std::make_unique<TinyProcessLib::Process>(command, path.string(), [this](const char *bytes, size_t n) {
      async_print(std::string(bytes, n));
    }, [this](const char *bytes, size_t n) {
      async_print(std::string(bytes, n), true);
    });
  else
    process = std::make_unique<TinyProcessLib::Process>(command, path.string());

  if(process->get_id() <= 0) {
    async_print("Error: failed to run command: " + command + "\n", true);
    return -1;
  }

  return process->get_exit_status();
}

int Terminal::process(std::istream &stdin_stream, std::ostream &stdout_stream, const std::string &command, const boost::filesystem::path &path, std::ostream *stderr_stream) {
  TinyProcessLib::Process process(command, path.string(), [&stdout_stream](const char *bytes, size_t n) {
    Glib::ustring umessage(std::string(bytes, n));
    Glib::ustring::iterator iter;
    while(!umessage.validate(iter)) {
      auto next_char_iter = iter;
      next_char_iter++;
      umessage.replace(iter, next_char_iter, "?");
    }
    stdout_stream.write(umessage.data(), n);
  }, [this, stderr_stream](const char *bytes, size_t n) {
    if(stderr_stream)
      stderr_stream->write(bytes, n);
    else
      async_print(std::string(bytes, n), true);
  }, true);

  if(process.get_id() <= 0) {
    async_print("Error: failed to run command: " + command + "\n", true);
    return -1;
  }

  char buffer[131072];
  for(;;) {
    stdin_stream.readsome(buffer, 131072);
    auto read_n = stdin_stream.gcount();
    if(read_n == 0)
      break;
    if(!process.write(buffer, read_n)) {
      break;
    }
  }
  process.close_stdin();

  return process.get_exit_status();
}

void Terminal::async_process(const std::string &command, const boost::filesystem::path &path, const std::function<void(int exit_status)> &callback, bool quiet) {
  std::thread async_execute_thread([this, command, path, callback, quiet]() {
    LockGuard lock(processes_mutex);
    stdin_buffer.clear();
    auto process = std::make_shared<TinyProcessLib::Process>(command, path.string(), [this, quiet](const char *bytes, size_t n) {
      if(!quiet)
        async_print(std::string(bytes, n));
    }, [this, quiet](const char *bytes, size_t n) {
      if(!quiet)
        async_print(std::string(bytes, n), true);
    }, true);
    auto pid = process->get_id();
    if(pid <= 0) {
      lock.unlock();
      async_print("Error: failed to run command: " + command + "\n", true);
      if(callback)
        callback(-1);
      return;
    }
    else {
      processes.emplace_back(process);
      lock.unlock();
    }

    auto exit_status = process->get_exit_status();

    lock.lock();
    for(auto it = processes.begin(); it != processes.end(); it++) {
      if((*it)->get_id() == pid) {
        processes.erase(it);
        break;
      }
    }
    stdin_buffer.clear();
    lock.unlock();

    if(callback)
      callback(exit_status);
  });
  async_execute_thread.detach();
}

void Terminal::kill_last_async_process(bool force) {
  LockGuard lock(processes_mutex);
  if(processes.empty())
    Info::get().print("No running processes");
  else
    processes.back()->kill(force);
}

void Terminal::kill_async_processes(bool force) {
  LockGuard lock(processes_mutex);
  for(auto &process : processes)
    process->kill(force);
}

bool Terminal::on_motion_notify_event(GdkEventMotion *event) {
  Gtk::TextIter iter;
  int location_x, location_y;
  window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, location_x, location_y);
  get_iter_at_location(iter, location_x, location_y);
  if(iter.has_tag(link_tag))
    get_window(Gtk::TextWindowType::TEXT_WINDOW_TEXT)->set_cursor(link_mouse_cursor);
  else
    get_window(Gtk::TextWindowType::TEXT_WINDOW_TEXT)->set_cursor(default_mouse_cursor);

    // Workaround for drag-and-drop crash on MacOS
    // TODO 2018: check if this bug has been fixed
#ifdef __APPLE__
  if((event->state & GDK_BUTTON1_MASK) == 0)
    return Gtk::TextView::on_motion_notify_event(event);
  else {
    int x, y;
    window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, x, y);
    Gtk::TextIter iter;
    get_iter_at_location(iter, x, y);
    get_buffer()->select_range(get_buffer()->get_insert()->get_iter(), iter);
    return true;
  }
#else
  return Gtk::TextView::on_motion_notify_event(event);
#endif

  return Gtk::TextView::on_motion_notify_event(event);
}

boost::optional<Terminal::Link> Terminal::find_link(const std::string &line, size_t pos, size_t length) {
  const static std::regex link_regex("^([A-Z]:)?([^:]+):([0-9]+):([0-9]+): .*$|"                      // C/C++ compile warning/error/rename usages
                                     "^In file included from ([A-Z]:)?([^:]+):([0-9]+)[:,]$|"         // C/C++ extra compile warning/error info
                                     "^                 from ([A-Z]:)?([^:]+):([0-9]+)[:,]$|"         // C/C++ extra compile warning/error info (gcc)
                                     "^  --> ([A-Z]:)?([^:]+):([0-9]+):([0-9]+)$|"                    // Rust
                                     "^Assertion failed: .*file ([A-Z]:)?([^:]+), line ([0-9]+)\\.$|" // clang assert()
                                     "^[^:]*: ([A-Z]:)?([^:]+):([0-9]+): .* Assertion .* failed\\.$|" // gcc assert()
                                     "^ERROR:([A-Z]:)?([^:]+):([0-9]+):.*$|"                          // g_assert (glib.h)
                                     "^([A-Z]:)?([\\/][^:]+):([0-9]+)$|"                              // Node.js
                                     "^  File \"([A-Z]:)?([^\"]+)\", line ([0-9]+), in .*$",          // Python
                                     std::regex::optimize);
  std::smatch sm;
  if(std::regex_match(line.cbegin() + pos,
                      line.cbegin() + (length == std::string::npos ? line.size() : std::min(pos + length, line.size())),
                      sm, link_regex)) {
    for(size_t sub = 1; sub < link_regex.mark_count();) {
      size_t subs = sub == 1 || sub == 4 + 3 + 3 + 1 ? 4 : 3;
      if(sm.length(sub + 1)) {
        auto start_pos = static_cast<int>(sm.position(sub + 1) - sm.length(sub));
        auto end_pos = static_cast<int>(sm.position(sub + subs - 1) + sm.length(sub + subs - 1));
        int start_pos_utf8 = utf8_character_count(line, 0, start_pos);
        int end_pos_utf8 = start_pos_utf8 + utf8_character_count(line, start_pos, end_pos - start_pos);
        std::string path;
        if(sm.length(sub))
          path = sm[sub].str();
        path += sm[sub + 1].str();
        try {
          auto line_number = std::stoi(sm[sub + 2].str());
          auto line_offset = std::stoi(subs == 4 ? sm[sub + 3].str() : "1");
          return Link{start_pos_utf8, end_pos_utf8, path, line_number, line_offset};
        }
        catch(...) {
          return {};
        }
      }
      sub += subs;
    }
  }
  return {};
}

void Terminal::print(std::string message, bool bold) {
  if(message.empty())
    return;

  if(auto parent = get_parent()) {
    if(!parent->is_visible())
      parent->show();
  }

#ifdef _WIN32
  // Remove color codes
  size_t pos = 0;
  while((pos = message.find('\e', pos)) != std::string::npos) {
    if((pos + 2) >= message.size())
      break;
    if(message[pos + 1] == '[') {
      size_t end_pos = pos + 2;
      bool color_code_found = false;
      while(end_pos < message.size()) {
        if((message[end_pos] >= '0' && message[end_pos] <= '9') || message[end_pos] == ';')
          end_pos++;
        else if(message[end_pos] == 'm') {
          color_code_found = true;
          break;
        }
        else
          break;
      }
      if(color_code_found)
        message.erase(pos, end_pos - pos + 1);
    }
  }
  Glib::ustring umessage = std::move(message);
#else
  Glib::ustring umessage = std::move(message);
#endif

  Glib::ustring::iterator iter;
  while(!umessage.validate(iter)) {
    auto next_char_iter = iter;
    next_char_iter++;
    umessage.replace(iter, next_char_iter, "?");
  }

  if(bold)
    get_buffer()->insert_with_tag(get_buffer()->end(), umessage, bold_tag);
  else
    get_buffer()->insert(get_buffer()->end(), umessage);

  if(get_buffer()->get_line_count() > Config::get().terminal.history_size) {
    int lines = get_buffer()->get_line_count() - Config::get().terminal.history_size;
    get_buffer()->erase(get_buffer()->begin(), get_buffer()->get_iter_at_line(lines));
    deleted_lines += static_cast<size_t>(lines);
  }
}

void Terminal::async_print(std::string message, bool bold) {
  dispatcher.post([message = std::move(message), bold]() mutable {
    Terminal::get().print(std::move(message), bold);
  });
}

void Terminal::configure() {
  link_tag->property_foreground_rgba() = get_style_context()->get_color(Gtk::StateFlags::STATE_FLAG_LINK);

  // Set search match style:
  get_buffer()->get_tag_table()->foreach([](const Glib::RefPtr<Gtk::TextTag> &tag) {
    if(tag->property_background_set()) {
      auto scheme = Source::StyleSchemeManager::get_default()->get_scheme(Config::get().source.style);
      if(scheme) {
        auto style = scheme->get_style("search-match");
        if(style) {
          if(style->property_background_set())
            tag->property_background() = style->property_background();
          if(style->property_foreground_set())
            tag->property_foreground() = style->property_foreground();
        }
      }
    }
  });
}

void Terminal::clear() {
  get_buffer()->set_text("");
}

bool Terminal::on_button_press_event(GdkEventButton *button_event) {
  //open clicked link in terminal
  if(button_event->type == GDK_BUTTON_PRESS && button_event->button == GDK_BUTTON_PRIMARY) {
    Gtk::TextIter iter;
    int location_x, location_y;
    window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, button_event->x, button_event->y, location_x, location_y);
    get_iter_at_location(iter, location_x, location_y);
    if(iter.has_tag(link_tag)) {
      auto start_iter = get_buffer()->get_iter_at_line(iter.get_line());
      auto end_iter = start_iter;
      while(!end_iter.ends_line() && end_iter.forward_char()) {
      }
      auto link = find_link(get_buffer()->get_text(start_iter, end_iter).raw());
      if(link) {
        auto path = filesystem::get_long_path(link->path);

        if(path.is_relative()) {
          if(Project::current) {
            boost::system::error_code ec;
            if(boost::filesystem::exists(Project::current->build->get_default_path() / path, ec))
              path = Project::current->build->get_default_path() / path;
            else if(boost::filesystem::exists(Project::current->build->get_debug_path() / path, ec))
              path = Project::current->build->get_debug_path() / path;
            else if(boost::filesystem::exists(Project::current->build->project_path / path, ec))
              path = Project::current->build->project_path / path;
            else
              return Gtk::TextView::on_button_press_event(button_event);
          }
          else
            return Gtk::TextView::on_button_press_event(button_event);
        }
        boost::system::error_code ec;
        if(boost::filesystem::is_regular_file(path, ec)) {
          if(Notebook::get().open(path)) {
            auto view = Notebook::get().get_current_view();
            view->place_cursor_at_line_index(link->line - 1, link->line_index - 1);
            view->scroll_to_cursor_delayed(true, true);
            return true;
          }
        }
      }
    }
  }
  return Gtk::TextView::on_button_press_event(button_event);
}

bool Terminal::on_key_press_event(GdkEventKey *event) {
  if(event->keyval == GDK_KEY_Home || event->keyval == GDK_KEY_End ||
     event->keyval == GDK_KEY_Page_Up || event->keyval == GDK_KEY_Page_Down ||
     event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down ||
     event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right)
    return Source::SearchView::on_key_press_event(event);

  LockGuard lock(processes_mutex);
  bool debug_is_running = false;
#ifdef JUCI_ENABLE_DEBUG
  debug_is_running = Project::current ? Project::current->debug_is_running() : false;
#endif
  if(processes.size() > 0 || debug_is_running) {
    auto unicode = gdk_keyval_to_unicode(event->keyval);
    if(unicode >= 32 && unicode != 126 && unicode != 0) {
      get_buffer()->place_cursor(get_buffer()->end());
      stdin_buffer += unicode;
      get_buffer()->insert_at_cursor(stdin_buffer.substr(stdin_buffer.size() - 1));
    }
    else if(event->keyval == GDK_KEY_BackSpace) {
      get_buffer()->place_cursor(get_buffer()->end());
      if(stdin_buffer.size() > 0 && get_buffer()->get_char_count() > 0) {
        auto iter = get_buffer()->end();
        iter--;
        stdin_buffer.erase(stdin_buffer.size() - 1);
        get_buffer()->erase(iter, get_buffer()->end());
      }
    }
    else if(event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
      get_buffer()->place_cursor(get_buffer()->end());
      stdin_buffer += '\n';
      if(debug_is_running) {
#ifdef JUCI_ENABLE_DEBUG
        Project::current->debug_write(stdin_buffer);
#endif
      }
      else
        processes.back()->write(stdin_buffer);
      get_buffer()->insert_at_cursor(stdin_buffer.substr(stdin_buffer.size() - 1));
      stdin_buffer.clear();
    }
  }
  return true;
}
