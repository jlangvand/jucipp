#include "notebook.h"
#include "config.h"
#include "filesystem.h"
#include "gtksourceview-3.0/gtksourceview/gtksourcemap.h"
#include "project.h"
#include "selection_dialog.h"
#include "source_clang.h"
#include "source_generic.h"
#include "source_language_protocol.h"
#include <fstream>
#include <regex>

Notebook::TabLabel::TabLabel(const std::function<void()> &on_close) {
  set_can_focus(false);

  auto button = Gtk::manage(new Gtk::Button());
  auto hbox = Gtk::manage(new Gtk::Box());

  hbox->set_can_focus(false);
  label.set_can_focus(false);
  button->set_image_from_icon_name("window-close-symbolic", Gtk::ICON_SIZE_MENU);
  button->set_can_focus(false);
  button->set_relief(Gtk::ReliefStyle::RELIEF_NONE);

  hbox->pack_start(label, Gtk::PACK_SHRINK);
  hbox->pack_end(*button, Gtk::PACK_SHRINK);
  add(*hbox);

  button->signal_clicked().connect(on_close);
  signal_button_press_event().connect([on_close](GdkEventButton *event) {
    if(event->button == GDK_BUTTON_MIDDLE) {
      on_close();
      return true;
    }
    return false;
  });

  show_all();
}

Notebook::Notebook() : Gtk::Paned(), notebooks(2) {
  for(auto &notebook : notebooks) {
    notebook.get_style_context()->add_class("juci_notebook");
    notebook.set_scrollable();
    notebook.set_group_name("source_notebooks");
    notebook.signal_switch_page().connect([this](Gtk::Widget *widget, guint) {
      auto hbox = dynamic_cast<Gtk::Box *>(widget);
      for(size_t c = 0; c < hboxes.size(); ++c) {
        if(hboxes[c].get() == hbox) {
          focus_view(source_views[c]);
          set_current_view(source_views[c]);
          break;
        }
      }
      last_index = -1;
    });
    notebook.signal_page_added().connect([this](Gtk::Widget *widget, guint) {
      auto hbox = dynamic_cast<Gtk::Box *>(widget);
      for(size_t c = 0; c < hboxes.size(); ++c) {
        if(hboxes[c].get() == hbox) {
          focus_view(source_views[c]);
          set_current_view(source_views[c]);
          break;
        }
      }
    });
  }
  pack1(notebooks[0], true, true);
}

size_t Notebook::size() {
  return source_views.size();
}

Source::View *Notebook::get_view(size_t index) {
  if(index >= size())
    return nullptr;
  return source_views[index];
}

Source::View *Notebook::get_current_view() {
  if(intermediate_view) {
    for(auto view : source_views) {
      if(view == intermediate_view)
        return view;
    }
  }
  for(auto view : source_views) {
    if(view == current_view)
      return view;
  }
  //In case there exist a tab that has not yet received focus again in a different notebook
  for(int notebook_index = 0; notebook_index < 2; ++notebook_index) {
    auto page = notebooks[notebook_index].get_current_page();
    if(page >= 0)
      return get_view(notebook_index, page);
  }
  return nullptr;
}

std::vector<Source::View *> &Notebook::get_views() {
  return source_views;
}

void Notebook::open(const boost::filesystem::path &file_path_, Position position) {
  boost::system::error_code ec;
  if(file_path_.empty() || (boost::filesystem::exists(file_path_, ec) && !boost::filesystem::is_regular_file(file_path_, ec))) {
    Terminal::get().print("Error: could not open " + file_path_.string() + "\n", true);
    return;
  }

  auto file_path = filesystem::get_normal_path(file_path_);

  if((position == Position::right || position == Position::split) && !split)
    toggle_split();

  // Use canonical path to follow symbolic links
  if(position == Position::infer) {
    auto canonical_file_path = filesystem::get_canonical_path(file_path);
    for(size_t c = 0; c < size(); c++) {
      bool equal;
      {
        LockGuard lock(source_views[c]->canonical_file_path_mutex);
        equal = canonical_file_path == source_views[c]->canonical_file_path;
      }
      if(equal) {
        auto notebook_page = get_notebook_page(c);
        notebooks[notebook_page.first].set_current_page(notebook_page.second);
        focus_view(source_views[c]);
        return;
      }
    }
  }

  if(boost::filesystem::exists(file_path, ec)) {
    std::ifstream can_read(file_path.string());
    if(!can_read) {
      Terminal::get().print("Error: could not open " + file_path.string() + "\n", true);
      return;
    }
    can_read.close();
  }

  auto last_view = get_current_view();

  auto language = Source::guess_language(file_path);

  std::string language_protocol_language_id;
  if(language) {
    language_protocol_language_id = language->get_id();
    if(language_protocol_language_id == "js") {
      if(file_path.extension() == ".ts")
        language_protocol_language_id = "typescript";
      else if(file_path.extension() == ".tsx")
        language_protocol_language_id = "typescriptreact";
      else
        language_protocol_language_id = "javascript";
    }
  }

  if(language && (language->get_id() == "chdr" || language->get_id() == "cpphdr" || language->get_id() == "c" || language->get_id() == "cpp" || language->get_id() == "objc"))
    source_views.emplace_back(new Source::ClangView(file_path, language));
  else if(language && !language_protocol_language_id.empty() && !filesystem::find_executable(language_protocol_language_id + "-language-server").empty())
    source_views.emplace_back(new Source::LanguageProtocolView(file_path, language, language_protocol_language_id));
  else
    source_views.emplace_back(new Source::GenericView(file_path, language));

  auto view = source_views.back();

  if(position == Position::split) {
    auto previous_view = get_current_view();
    if(previous_view) {
      view->replace_text(previous_view->get_buffer()->get_text());
      position = get_notebook_page(get_index(previous_view)).first == 0 ? Position::right : Position::left;
    }
  }

  view->configure();

  view->update_status_location = [this](Source::BaseView *view) {
    if(get_current_view() == view) {
      auto iter = view->get_buffer()->get_insert()->get_iter();
      auto status_location_text = ' ' + std::to_string(iter.get_line() + 1) + ':' + std::to_string(iter.get_line_offset() + 1);

      if(view->get_buffer()->get_has_selection()) {
        Gtk::TextIter start, end;
        view->get_buffer()->get_selection_bounds(start, end);
        if(start > end)
          std::swap(start, end);
        int lines = end.get_line() - start.get_line() + (end.starts_line() ? 0 : 1); // Do not count lines where the end iter is at start of line
        int words = 0;
        bool in_word = false;
        auto iter = start;
        do {
          if(*iter <= ' ')
            in_word = false;
          else if(!in_word) {
            ++words;
            in_word = true;
          }
        } while(iter.forward_char() && iter < end);
        int chars = end.get_offset() - start.get_offset();
        status_location_text += " (" + std::to_string(lines) + ':' + std::to_string(words) + ':' + std::to_string(chars) + ')';
      }

      status_location.set_text(status_location_text);
    }
  };
  view->update_status_file_path = [this](Source::BaseView *view) {
    if(get_current_view() == view)
      status_file_path.set_text(' ' + filesystem::get_short_path(view->file_path).string());
  };
  view->update_status_branch = [this](Source::BaseView *view) {
    if(get_current_view() == view) {
      if(!view->status_branch.empty())
        status_branch.set_text(" (" + view->status_branch + ")");
      else
        status_branch.set_text("");
    }
  };
  view->update_status_diagnostics = [this](Source::BaseView *view) {
    if(get_current_view() == view) {
      std::string diagnostic_info;

      auto num_warnings = std::get<0>(view->status_diagnostics);
      auto num_errors = std::get<1>(view->status_diagnostics);
      auto num_fix_its = std::get<2>(view->status_diagnostics);
      if(num_warnings > 0 || num_errors > 0 || num_fix_its > 0) {
        auto normal_color = status_diagnostics.get_style_context()->get_color(Gtk::StateFlags::STATE_FLAG_NORMAL);
        auto light_theme = (normal_color.get_red() + normal_color.get_green() + normal_color.get_blue()) / 3 < 0.5;

        Gdk::RGBA yellow;
        yellow.set_rgba(1.0, 1.0, 0.2);
        double factor = 0.5;
        yellow.set_red(normal_color.get_red() + factor * (yellow.get_red() - normal_color.get_red()));
        yellow.set_green(normal_color.get_green() + factor * (yellow.get_green() - normal_color.get_green()));
        yellow.set_blue(normal_color.get_blue() + factor * (yellow.get_blue() - normal_color.get_blue()));
        Gdk::RGBA red;
        red.set_rgba(1.0, 0.0, 0.0);
        factor = light_theme ? 0.5 : 0.35;
        red.set_red(normal_color.get_red() + factor * (red.get_red() - normal_color.get_red()));
        red.set_green(normal_color.get_green() + factor * (red.get_green() - normal_color.get_green()));
        red.set_blue(normal_color.get_blue() + factor * (red.get_blue() - normal_color.get_blue()));
        Gdk::RGBA green;
        green.set_rgba(0.0, 1.0, 0.0);
        factor = 0.4;
        green.set_red(normal_color.get_red() + factor * (green.get_red() - normal_color.get_red()));
        green.set_green(normal_color.get_green() + factor * (green.get_green() - normal_color.get_green()));
        green.set_blue(normal_color.get_blue() + factor * (green.get_blue() - normal_color.get_blue()));

        std::stringstream yellow_ss, red_ss, green_ss;
        yellow_ss << std::hex << std::setfill('0') << std::setw(2) << (int)(yellow.get_red_u() >> 8) << std::setw(2) << (int)(yellow.get_green_u() >> 8) << std::setw(2) << (int)(yellow.get_blue_u() >> 8);
        red_ss << std::hex << std::setfill('0') << std::setw(2) << (int)(red.get_red_u() >> 8) << std::setw(2) << (int)(red.get_green_u() >> 8) << std::setw(2) << (int)(red.get_blue_u() >> 8);
        green_ss << std::hex << std::setfill('0') << std::setw(2) << (int)(green.get_red_u() >> 8) << std::setw(2) << (int)(green.get_green_u() >> 8) << std::setw(2) << (int)(green.get_blue_u() >> 8);
        if(num_warnings > 0) {
          diagnostic_info += "<span color='#" + yellow_ss.str() + "'>";
          diagnostic_info += std::to_string(num_warnings) + " warning";
          if(num_warnings > 1)
            diagnostic_info += 's';
          diagnostic_info += "</span>";
        }
        if(num_errors > 0) {
          if(num_warnings > 0)
            diagnostic_info += ", ";
          diagnostic_info += "<span color='#" + red_ss.str() + "'>";
          diagnostic_info += std::to_string(num_errors) + " error";
          if(num_errors > 1)
            diagnostic_info += 's';
          diagnostic_info += "</span>";
        }
        if(num_fix_its > 0) {
          if(num_warnings > 0 || num_errors > 0)
            diagnostic_info += ", ";
          diagnostic_info += "<span color='#" + green_ss.str() + "'>";
          diagnostic_info += std::to_string(num_fix_its) + " fix it";
          if(num_fix_its > 1)
            diagnostic_info += 's';
          diagnostic_info += "</span>";
        }
      }
      status_diagnostics.set_markup(diagnostic_info);
    }
  };
  view->update_status_state = [this](Source::BaseView *view) {
    if(get_current_view() == view)
      status_state.set_text(view->status_state + " ");
  };

  scrolled_windows.emplace_back(new Gtk::ScrolledWindow());
  hboxes.emplace_back(new Gtk::Box());
  scrolled_windows.back()->add(*view);
  hboxes.back()->pack_start(*scrolled_windows.back());

  source_maps.emplace_back(Glib::wrap(gtk_source_map_new()));
  gtk_source_map_set_view(GTK_SOURCE_MAP(source_maps.back()->gobj()), view->gobj());
  source_maps.back()->get_style_context()->add_class("juci_source_map");

  configure(source_views.size() - 1);

  //Set up tab label
  tab_labels.emplace_back(new TabLabel([this, view]() {
    auto index = get_index(view);
    if(index != static_cast<size_t>(-1))
      close(index);
  }));
  view->update_tab_label = [this](Source::BaseView *view) {
    std::string title = view->file_path.filename().string();
    if(view->get_buffer()->get_modified())
      title += '*';
    else
      title += ' ';
    for(size_t c = 0; c < size(); ++c) {
      if(source_views[c] == view) {
        auto &tab_label = tab_labels.at(c);
        tab_label->label.set_text(title);
        tab_label->set_tooltip_text(filesystem::get_short_path(view->file_path).string());
        return;
      }
    }
  };
  view->update_tab_label(view);

  //Add star on tab label when the page is not saved:
  view->get_buffer()->signal_modified_changed().connect([view]() {
    if(view->update_tab_label)
      view->update_tab_label(view);
  });

  //Cursor history
  auto update_cursor_locations = [this, view](const Gtk::TextBuffer::iterator &iter) {
    bool mark_moved = false;
    if(current_cursor_location != static_cast<size_t>(-1)) {
      auto &cursor_location = cursor_locations.at(current_cursor_location);
      if(cursor_location.view == view && abs(cursor_location.mark->get_iter().get_line() - iter.get_line()) <= 2) {
        view->get_buffer()->move_mark(cursor_location.mark, iter);
        mark_moved = true;
      }
    }
    if(!mark_moved) {
      if(current_cursor_location != static_cast<size_t>(-1)) {
        for(auto it = cursor_locations.begin() + current_cursor_location + 1; it != cursor_locations.end();) {
          it->view->get_buffer()->delete_mark(it->mark);
          it = cursor_locations.erase(it);
        }
      }
      cursor_locations.emplace_back(view, view->get_buffer()->create_mark(iter));
      current_cursor_location = cursor_locations.size() - 1;
    }

    // Combine adjacent cursor histories that are similar
    if(!cursor_locations.empty()) {
      size_t cursor_locations_index = 1;
      auto last_it = cursor_locations.begin();
      for(auto it = cursor_locations.begin() + 1; it != cursor_locations.end();) {
        if(last_it->view == it->view && abs(last_it->mark->get_iter().get_line() - it->mark->get_iter().get_line()) <= 2) {
          last_it->view->get_buffer()->delete_mark(last_it->mark);
          last_it->mark = it->mark;
          it = cursor_locations.erase(it);
          if(current_cursor_location != static_cast<size_t>(-1) && current_cursor_location > cursor_locations_index)
            --current_cursor_location;
        }
        else {
          ++it;
          ++last_it;
          ++cursor_locations_index;
        }
      }
    }

    // Remove start of cache if cache limit is exceeded
    while(cursor_locations.size() > 10) {
      cursor_locations.begin()->view->get_buffer()->delete_mark(cursor_locations.begin()->mark);
      cursor_locations.erase(cursor_locations.begin());
      if(current_cursor_location != static_cast<size_t>(-1))
        --current_cursor_location;
    }

    if(current_cursor_location >= cursor_locations.size())
      current_cursor_location = cursor_locations.size() - 1;
  };
  view->get_buffer()->signal_mark_set().connect([this, update_cursor_locations](const Gtk::TextBuffer::iterator &iter, const Glib::RefPtr<Gtk::TextBuffer::Mark> &mark) {
    if(mark->get_name() == "insert") {
      if(disable_next_update_cursor_locations) {
        disable_next_update_cursor_locations = false;
        return;
      }
      update_cursor_locations(iter);
    }
  });
  view->get_buffer()->signal_changed().connect([view, update_cursor_locations] {
    update_cursor_locations(view->get_buffer()->get_insert()->get_iter());
  });

#ifdef JUCI_ENABLE_DEBUG
  if(dynamic_cast<Source::ClangView *>(view) || (view->language && view->language->get_id() == "rust")) {
    view->toggle_breakpoint = [view](int line_nr) {
      if(view->get_source_buffer()->get_source_marks_at_line(line_nr, "debug_breakpoint").size() > 0) {
        auto start_iter = view->get_buffer()->get_iter_at_line(line_nr);
        auto end_iter = view->get_iter_at_line_end(line_nr);
        view->get_source_buffer()->remove_source_marks(start_iter, end_iter, "debug_breakpoint");
        view->get_source_buffer()->remove_source_marks(start_iter, end_iter, "debug_breakpoint_and_stop");
        if(Project::current && Project::debugging)
          Project::current->debug_remove_breakpoint(view->file_path, line_nr + 1, view->get_buffer()->get_line_count() + 1);
      }
      else {
        auto iter = view->get_buffer()->get_iter_at_line(line_nr);
        gtk_source_buffer_create_source_mark(view->get_source_buffer()->gobj(), nullptr, "debug_breakpoint", iter.gobj()); // Gsv::Buffer::create_source_mark is bugged
        if(view->get_source_buffer()->get_source_marks_at_line(line_nr, "debug_stop").size() > 0)
          gtk_source_buffer_create_source_mark(view->get_source_buffer()->gobj(), nullptr, "debug_breakpoint_and_stop", iter.gobj()); // Gsv::Buffer::create_source_mark is bugged
        if(Project::current && Project::debugging)
          Project::current->debug_add_breakpoint(view->file_path, line_nr + 1);
      }
    };
  }
#endif

  view->signal_focus_in_event().connect([this, view](GdkEventFocus *) {
    if(on_focus_page)
      on_focus_page(view);
    set_current_view(view);
    return false;
  });

  if(position == Position::infer) {
    if(!split)
      position = Position::left;
    else if(notebooks[0].get_n_pages() == 0)
      position = Position::left;
    else if(notebooks[1].get_n_pages() == 0)
      position = Position::right;
    else if(last_view)
      position = get_notebook_page(get_index(last_view)).first == 0 ? Position::left : Position::right;
  }
  size_t notebook_index = position == Position::right ? 1 : 0;
  auto &notebook = notebooks[notebook_index];

  notebook.append_page(*hboxes.back(), *tab_labels.back());

  notebook.set_tab_reorderable(*hboxes.back(), true);
  notebook.set_tab_detachable(*hboxes.back(), true);
  show_all_children();

  notebook.set_current_page(notebook.get_n_pages() - 1);
  last_index = -1;
  if(last_view) {
    auto index = get_index(last_view);
    auto notebook_page = get_notebook_page(index);
    if(notebook_page.first == notebook_index)
      last_index = index;
  }

  set_focus_child(*source_views.back());
  focus_view(view);
}

void Notebook::open_uri(const std::string &uri) {
#ifdef __APPLE__
  Terminal::get().process("open " + filesystem::escape_argument(uri));
#else
  GError *error = nullptr;
#if GTK_VERSION_GE(3, 22)
  gtk_show_uri_on_window(nullptr, uri.c_str(), GDK_CURRENT_TIME, &error);
#else
  gtk_show_uri(nullptr, uri.c_str(), GDK_CURRENT_TIME, &error);
#endif
  g_clear_error(&error);
#endif
}


void Notebook::configure(size_t index) {
  if(Config::get().source.show_map) {
    if(hboxes.at(index)->get_children().size() == 1)
      hboxes.at(index)->pack_end(*source_maps.at(index), Gtk::PACK_SHRINK);
  }
  else if(hboxes.at(index)->get_children().size() == 2)
    hboxes.at(index)->remove(*source_maps.at(index));
}

bool Notebook::save(size_t index) {
  if(!source_views[index]->save())
    return false;
  Project::on_save(index);
  return true;
}

bool Notebook::save_current() {
  if(auto view = get_current_view())
    return save(get_index(view));
  return false;
}

bool Notebook::close(size_t index) {
  if(auto view = get_view(index)) {
    if(view->get_buffer()->get_modified()) {
      if(!save_modified_dialog(index))
        return false;
    }
    if(view == get_current_view()) {
      bool focused = false;
      if(last_index != static_cast<size_t>(-1)) {
        auto notebook_page = get_notebook_page(last_index);
        if(notebook_page.first == get_notebook_page(get_index(view)).first) {
          focus_view(source_views[last_index]);
          notebooks[notebook_page.first].set_current_page(notebook_page.second);
          last_index = -1;
          focused = true;
        }
      }
      if(!focused) {
        auto notebook_page = get_notebook_page(get_index(view));
        if(notebook_page.second > 0)
          focus_view(get_view(notebook_page.first, notebook_page.second - 1));
        else {
          size_t notebook_index = notebook_page.first == 0 ? 1 : 0;
          if(notebooks[notebook_index].get_n_pages() > 0)
            focus_view(get_view(notebook_index, notebooks[notebook_index].get_current_page()));
          else
            set_current_view(nullptr);
        }
      }
    }
    else if(index == last_index)
      last_index = -1;
    else if(index < last_index && last_index != static_cast<size_t>(-1))
      last_index--;

    auto notebook_page = get_notebook_page(index);
    notebooks[notebook_page.first].remove_page(notebook_page.second);
    source_maps.erase(source_maps.begin() + index);

    if(on_close_page)
      on_close_page(view);

    delete_cursor_locations(view);

    SelectionDialog::get() = nullptr;
    CompletionDialog::get() = nullptr;

    if(auto clang_view = dynamic_cast<Source::ClangView *>(view))
      clang_view->async_delete();
    else
      delete view;
    source_views.erase(source_views.begin() + index);
    scrolled_windows.erase(scrolled_windows.begin() + index);
    hboxes.erase(hboxes.begin() + index);
    tab_labels.erase(tab_labels.begin() + index);
  }
  return true;
}

void Notebook::delete_cursor_locations(Source::View *view) {
  size_t cursor_locations_index = 0;
  for(auto it = cursor_locations.begin(); it != cursor_locations.end();) {
    if(it->view == view) {
      view->get_buffer()->delete_mark(it->mark);
      it = cursor_locations.erase(it);
      if(current_cursor_location != static_cast<size_t>(-1) && current_cursor_location > cursor_locations_index)
        --current_cursor_location;
    }
    else {
      ++it;
      ++cursor_locations_index;
    }
  }
  if(current_cursor_location >= cursor_locations.size())
    current_cursor_location = cursor_locations.size() - 1;
}

bool Notebook::close_current() {
  return close(get_index(get_current_view()));
}

void Notebook::next() {
  if(auto view = get_current_view()) {
    auto notebook_page = get_notebook_page(get_index(view));
    int page = notebook_page.second + 1;
    if(page >= notebooks[notebook_page.first].get_n_pages())
      notebooks[notebook_page.first].set_current_page(0);
    else
      notebooks[notebook_page.first].set_current_page(page);
  }
}

void Notebook::previous() {
  if(auto view = get_current_view()) {
    auto notebook_page = get_notebook_page(get_index(view));
    int page = notebook_page.second - 1;
    if(page < 0)
      notebooks[notebook_page.first].set_current_page(notebooks[notebook_page.first].get_n_pages() - 1);
    else
      notebooks[notebook_page.first].set_current_page(page);
  }
}

void Notebook::toggle_split() {
  if(!split) {
    pack2(notebooks[1], true, true);
    set_position(get_width() / 2);
    show_all();
    //Make sure the position is correct
    //TODO: report bug to gtk if it is not fixed in gtk3.22
    Glib::signal_timeout().connect([this] {
      set_position(get_width() / 2);
      return false;
    }, 200);
  }
  else {
    for(size_t c = size() - 1; c != static_cast<size_t>(-1); --c) {
      auto notebook_index = get_notebook_page(c).first;
      if(notebook_index == 1 && !close(c))
        return;
    }
    remove(notebooks[1]);
  }
  split = !split;
}
void Notebook::toggle_tabs() {
  //Show / Hide tabs for each notebook.
  for(auto &notebook : Notebook::notebooks)
    notebook.set_show_tabs(!notebook.get_show_tabs());
}

std::vector<std::pair<size_t, Source::View *>> Notebook::get_notebook_views() {
  std::vector<std::pair<size_t, Source::View *>> notebook_views;
  for(size_t notebook_index = 0; notebook_index < notebooks.size(); ++notebook_index) {
    for(int page = 0; page < notebooks[notebook_index].get_n_pages(); ++page) {
      if(auto view = get_view(notebook_index, page))
        notebook_views.emplace_back(notebook_index, view);
    }
  }
  return notebook_views;
}

void Notebook::update_status(Source::BaseView *view) {
  if(view->update_status_location)
    view->update_status_location(view);
  if(view->update_status_file_path)
    view->update_status_file_path(view);
  if(view->update_status_branch)
    view->update_status_branch(view);
  if(view->update_status_diagnostics)
    view->update_status_diagnostics(view);
  if(view->update_status_state)
    view->update_status_state(view);
}

void Notebook::clear_status() {
  status_location.set_text("");
  status_file_path.set_text("");
  status_branch.set_text("");
  status_diagnostics.set_text("");
  status_state.set_text("");
}

size_t Notebook::get_index(Source::View *view) {
  for(size_t c = 0; c < size(); ++c) {
    if(source_views[c] == view)
      return c;
  }
  return -1;
}

Source::View *Notebook::get_view(size_t notebook_index, int page) {
  if(notebook_index == static_cast<size_t>(-1) || notebook_index >= notebooks.size() ||
     page < 0 || page >= notebooks[notebook_index].get_n_pages())
    return nullptr;
  auto hbox = dynamic_cast<Gtk::Box *>(notebooks[notebook_index].get_nth_page(page));
  auto scrolled_window = dynamic_cast<Gtk::ScrolledWindow *>(hbox->get_children()[0]);
  return dynamic_cast<Source::View *>(scrolled_window->get_children()[0]);
}

void Notebook::focus_view(Source::View *view) {
  intermediate_view = view;
  view->grab_focus();
}

std::pair<size_t, int> Notebook::get_notebook_page(size_t index) {
  if(index >= hboxes.size())
    return {-1, -1};
  for(size_t c = 0; c < notebooks.size(); ++c) {
    auto page_num = notebooks[c].page_num(*hboxes[index]);
    if(page_num >= 0)
      return {c, page_num};
  }
  return {-1, -1};
}

void Notebook::set_current_view(Source::View *view) {
  intermediate_view = nullptr;
  if(current_view != view) {
    if(auto view = get_current_view()) {
      view->hide_tooltips();
      view->hide_dialogs();
    }
    current_view = view;
    if(view && on_change_page)
      on_change_page(view);
  }
}

bool Notebook::save_modified_dialog(size_t index) {
  Gtk::MessageDialog dialog(*static_cast<Gtk::Window *>(get_toplevel()), "Save file!", false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO);
  dialog.set_default_response(Gtk::RESPONSE_YES);
  dialog.set_secondary_text("Do you want to save: " + get_view(index)->file_path.string() + " ?");
  int result = dialog.run();
  if(result == Gtk::RESPONSE_YES) {
    return save(index);
  }
  else if(result == Gtk::RESPONSE_NO) {
    return true;
  }
  else {
    return false;
  }
}
