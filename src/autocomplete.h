#pragma once
#include "dispatcher.h"
#include "mutex.h"
#include "tooltips.h"
#include <atomic>
#include <thread>

class Autocomplete {
  Gtk::TextView *view;
  bool &interactive_completion;
  /// If text_view buffer should be passed to add_rows. Empty buffer is passed if not.
  /// Also, some utilities, like libclang, require that autocomplete is started at the beginning of a word.
  bool pass_buffer_and_strip_word;

  Dispatcher dispatcher;

public:
  enum class State { IDLE, STARTING, RESTARTING, CANCELED };

  Mutex prefix_mutex;
  Glib::ustring prefix GUARDED_BY(prefix_mutex);
  std::vector<std::string> rows;
  Tooltips tooltips;

  std::atomic<State> state = {State::IDLE};

  std::thread thread;

  std::function<bool()> is_processing = [] { return true; };
  std::function<void()> reparse = [] {};
  std::function<void()> cancel_reparse = [] {};
  std::function<std::unique_ptr<LockGuard>()> get_parse_lock = [] { return nullptr; };
  std::function<void()> stop_parse = [] {};

  std::function<bool(guint last_keyval)> is_continue_key = [](guint) { return false; };
  std::function<bool(guint last_keyval)> is_restart_key = [](guint) { return false; };
  std::function<bool()> run_check = [] { return false; };

  std::function<void()> before_add_rows = [] {};
  std::function<void()> after_add_rows = [] {};
  std::function<void()> on_add_rows_error = [] {};

  /// The handler is not run in the main loop.
  std::function<void(std::string &buffer, int line_number, int column)> add_rows = [](std::string &, int, int) {};

  std::function<void()> on_show = [] {};
  std::function<void()> on_hide = [] {};
  std::function<void(unsigned int, const std::string &)> on_changed = [](unsigned int index, const std::string &text) {};
  std::function<void(unsigned int, const std::string &, bool)> on_select = [](unsigned int index, const std::string &text, bool hide_window) {};

  std::function<std::string(unsigned int)> get_tooltip = [](unsigned int index) { return std::string(); };

  Autocomplete(Gtk::TextView *view, bool &interactive_completion, guint &last_keyval, bool pass_buffer_and_strip_word);

  void run();
  void stop();

private:
  void setup_dialog();
};
