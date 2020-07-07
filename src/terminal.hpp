#pragma once
#include "dispatcher.hpp"
#include "mutex.hpp"
#include "process.hpp"
#include "source_base.hpp"
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <gtkmm.h>
#include <iostream>
#include <tuple>

class Terminal : public Source::SearchView {
  Terminal();

public:
  static Terminal &get() {
    static Terminal singleton;
    return singleton;
  }

  int process(const std::string &command, const boost::filesystem::path &path = "", bool use_pipes = true);
  int process(std::istream &stdin_stream, std::ostream &stdout_stream, const std::string &command, const boost::filesystem::path &path = "", std::ostream *stderr_stream = nullptr);
  void async_process(const std::string &command, const boost::filesystem::path &path = "", const std::function<void(int exit_status)> &callback = nullptr, bool quiet = false);
  void kill_last_async_process(bool force = false);
  void kill_async_processes(bool force = false);

  void print(std::string message, bool bold = false);
  void async_print(std::string message, bool bold = false);

  void configure();

  void clear();

protected:
  bool on_motion_notify_event(GdkEventMotion *motion_event) override;
  bool on_button_press_event(GdkEventButton *button_event) override;
  bool on_key_press_event(GdkEventKey *event) override;

private:
  Dispatcher dispatcher;
  Glib::RefPtr<Gtk::TextTag> bold_tag;
  Glib::RefPtr<Gtk::TextTag> link_tag;
  Glib::RefPtr<Gdk::Cursor> link_mouse_cursor;
  Glib::RefPtr<Gdk::Cursor> default_mouse_cursor;
  size_t deleted_lines = 0;

  struct Link {
    int start_pos, end_pos;
    std::string path;
    int line, line_index;
  };
  boost::optional<Link> find_link(const std::string &line, size_t pos = 0, size_t length = std::string::npos);

  Mutex processes_mutex;
  std::vector<std::shared_ptr<TinyProcessLib::Process>> processes GUARDED_BY(processes_mutex);
  Glib::ustring stdin_buffer;
};
