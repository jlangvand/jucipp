#include "autocomplete.hpp"
#include "selection_dialog.hpp"

Autocomplete::Autocomplete(Gsv::View *view, bool &interactive_completion, guint &last_keyval, bool pass_buffer_and_strip_word)
    : view(view), interactive_completion(interactive_completion), pass_buffer_and_strip_word(pass_buffer_and_strip_word) {
  view->get_buffer()->signal_changed().connect([this, &last_keyval] {
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible()) {
      cancel_reparse();
      return;
    }
    if(!this->view->has_focus())
      return;
    if(is_continue_key(last_keyval) && (this->interactive_completion || state != State::idle))
      run();
    else {
      stop();

      if(is_restart_key(last_keyval) && this->interactive_completion)
        run();
    }
  });

  view->get_buffer()->signal_mark_set().connect([this](const Gtk::TextIter &iterator, const Glib::RefPtr<Gtk::TextBuffer::Mark> &mark) {
    if(mark->get_name() == "insert")
      stop();
  });

  view->signal_key_release_event().connect([](GdkEventKey *event) {
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible()) {
      if(CompletionDialog::get()->on_key_release(event))
        return true;
    }
    return false;
  }, false);

  view->signal_focus_out_event().connect([this](GdkEventFocus *event) {
    stop();
    return false;
  });
}

void Autocomplete::run() {
  if(run_check()) {
    if(!is_processing())
      return;

    if(state == State::canceled)
      state = State::restarting;

    if(state != State::idle)
      return;

    state = State::starting;

    before_add_rows();

    if(thread.joinable())
      thread.join();
    auto iter = view->get_buffer()->get_insert()->get_iter();
    auto line_nr = iter.get_line() + 1;
    auto column_nr = iter.get_line_index() + 1;
    Glib::ustring buffer;
    if(pass_buffer_and_strip_word) {
      auto pos = iter.get_offset() - 1;
      buffer = view->get_buffer()->get_text();
      while(pos >= 0 && ((buffer[pos] >= 'a' && buffer[pos] <= 'z') || (buffer[pos] >= 'A' && buffer[pos] <= 'Z') ||
                         (buffer[pos] >= '0' && buffer[pos] <= '9') || buffer[pos] == '_')) {
        buffer.replace(pos, 1, " ");
        column_nr--;
        pos--;
      }
    }
    thread = std::thread([this, line_nr, column_nr, buffer = std::move(buffer)] {
      auto lock = get_parse_lock();
      if(!is_processing())
        return;
      stop_parse();

      auto &buffer_raw = const_cast<std::string &>(buffer.raw());
      rows.clear();
      add_rows(buffer_raw, line_nr, column_nr);

      if(is_processing()) {
        dispatcher.post([this]() {
          after_add_rows();
          if(state == State::restarting) {
            state = State::idle;
            reparse();
            run();
          }
          else if(state == State::canceled || rows.empty()) {
            state = State::idle;
            reparse();
          }
          else {
            auto start_iter = view->get_buffer()->get_insert()->get_iter();
            std::size_t prefix_size;
            {
              LockGuard lock(prefix_mutex);
              prefix_size = prefix.size();
            }
            if(prefix_size > 0 && !start_iter.backward_chars(prefix_size)) {
              state = State::idle;
              reparse();
              return;
            }
            CompletionDialog::create(view, start_iter);
            setup_dialog();
            for(auto &row : rows) {
              CompletionDialog::get()->add_row(row);
              row.clear();
            }
            state = State::idle;

            view->get_buffer()->begin_user_action();
            CompletionDialog::get()->show();
          }
        });
      }
      else {
        dispatcher.post([this] {
          state = State::canceled;
          on_add_rows_error();
        });
      }
    });
  }

  if(state != State::idle)
    cancel_reparse();
}

void Autocomplete::stop() {
  if(state == State::starting || state == State::restarting)
    state = State::canceled;
}

void Autocomplete::setup_dialog() {
  CompletionDialog::get()->on_show = [this] {
    on_show();
  };

  CompletionDialog::get()->on_hide = [this]() {
    view->get_buffer()->end_user_action();
    tooltips.hide();
    tooltips.clear();
    on_hide();
    reparse();
  };

  CompletionDialog::get()->on_change = [this](boost::optional<unsigned int> index, const std::string &text) {
    if(on_change)
      on_change(index, text);

    if(!index) {
      tooltips.hide();
      return;
    }

    auto set_buffer = set_tooltip_buffer(*index);
    if(!set_buffer)
      tooltips.hide();
    else {
      tooltips.clear();
      auto iter = CompletionDialog::get()->start_mark->get_iter();
      tooltips.emplace_back(view, iter, iter, [set_buffer = std::move(set_buffer)](Tooltip &tooltip) {
        set_buffer(tooltip);
      });
      tooltips.show(true);
    }
  };

  CompletionDialog::get()->on_select = [this](unsigned int index, const std::string &text, bool hide_window) {
    if(on_select)
      on_select(index, text, hide_window);
  };
}
