#include "tooltips.h"
#include "config.h"
#include "filesystem.h"
#include "info.h"
#include "notebook.h"
#include "selection_dialog.h"
#include <algorithm>
#include <regex>

std::set<Tooltip *> Tooltips::shown_tooltips;
Gdk::Rectangle Tooltips::drawn_tooltips_rectangle = Gdk::Rectangle();

Tooltip::Tooltip(Gtk::TextView *text_view, Glib::RefPtr<Gtk::TextBuffer::Mark> start_mark_,
                 Glib::RefPtr<Gtk::TextBuffer::Mark> end_mark_, std::function<void(Tooltip &toolip)> set_buffer_)
    : start_mark(std::move(start_mark_)), end_mark(std::move(end_mark_)), text_view(text_view), set_buffer(std::move(set_buffer_)) {}

Tooltip::~Tooltip() {
  Tooltips::shown_tooltips.erase(this);
  if(text_view) {
    text_view->get_buffer()->delete_mark(start_mark);
    text_view->get_buffer()->delete_mark(end_mark);
  }
}

void Tooltip::update() {
  if(text_view) {
    auto iter = start_mark->get_iter();
    auto end_iter = end_mark->get_iter();
    text_view->get_iter_location(iter, activation_rectangle);
    if(iter.get_offset() < end_iter.get_offset()) {
      while(iter.forward_char() && iter != end_iter) {
        Gdk::Rectangle rectangle;
        text_view->get_iter_location(iter, rectangle);
        activation_rectangle.join(rectangle);
      }
    }
    int location_window_x, location_window_y;
    text_view->buffer_to_window_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, activation_rectangle.get_x(), activation_rectangle.get_y(), location_window_x, location_window_y);
    activation_rectangle.set_x(location_window_x);
    activation_rectangle.set_y(location_window_y);
  }
}

void Tooltip::show(bool disregard_drawn, const std::function<void()> &on_motion) {
  Tooltips::shown_tooltips.emplace(this);

  if(!window) {
    //init window
    window = std::make_unique<Gtk::Window>(Gtk::WindowType::WINDOW_POPUP);

    auto g_application = g_application_get_default();
    auto gio_application = Glib::wrap(g_application, true);
    auto application = Glib::RefPtr<Gtk::Application>::cast_static(gio_application);
    if(auto active_window = application->get_active_window())
      window->set_transient_for(*active_window);

    window->set_type_hint(Gdk::WindowTypeHint::WINDOW_TYPE_HINT_TOOLTIP);

    window->set_events(Gdk::POINTER_MOTION_MASK);
    window->property_decorated() = false;
    window->set_accept_focus(false);
    window->set_skip_taskbar_hint(true);
    window->set_default_size(0, 0);

    window->signal_motion_notify_event().connect([on_motion](GdkEventMotion *event) {
      if(on_motion)
        on_motion();
      return false;
    });

    window->get_style_context()->add_class("juci_tooltip_window");
    auto visual = window->get_screen()->get_rgba_visual();
    if(visual)
      gtk_widget_set_visual(reinterpret_cast<GtkWidget *>(window->gobj()), visual->gobj());

    auto box = Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
    box->get_style_context()->add_class("juci_tooltip_box");
    window->add(*box);

    buffer = Gtk::TextBuffer::create();

    if(text_view) {
      auto tag = text_view->get_buffer()->get_tag_table()->lookup("def:warning");
      auto new_tag = buffer->create_tag("def:warning");
      if(tag->property_foreground_set())
        new_tag->property_foreground_rgba() = tag->property_foreground_rgba().get_value();
      if(tag->property_background_set())
        new_tag->property_background_rgba() = tag->property_background_rgba().get_value();

      tag = text_view->get_buffer()->get_tag_table()->lookup("def:error");
      new_tag = buffer->create_tag("def:error");
      if(tag->property_foreground_set())
        new_tag->property_foreground_rgba() = tag->property_foreground_rgba().get_value();
      if(tag->property_background_set())
        new_tag->property_background_rgba() = tag->property_background_rgba().get_value();
    }
    link_tag = buffer->create_tag("link");
    link_tag->property_underline() = Pango::Underline::UNDERLINE_SINGLE;
    link_tag->property_foreground_rgba() = window->get_style_context()->get_color(Gtk::StateFlags::STATE_FLAG_LINK);

    set_buffer(*this);

    remove_trailing_newlines();

    wrap_lines();

    auto tooltip_text_view = Gtk::manage(new Gtk::TextView(buffer));
    tooltip_text_view->get_style_context()->add_class("juci_tooltip_text_view");
    tooltip_text_view->set_editable(false);

    static auto link_mouse_cursor = Gdk::Cursor::create(Gdk::CursorType::HAND1);
    static auto default_mouse_cursor = Gdk::Cursor::create(Gdk::CursorType::XTERM);
    tooltip_text_view->signal_motion_notify_event().connect([this, tooltip_text_view](GdkEventMotion *event) {
      Gtk::TextIter iter;
      int location_x, location_y;
      tooltip_text_view->window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, location_x, location_y);
      tooltip_text_view->get_iter_at_location(iter, location_x, location_y);
      if(iter.has_tag(link_tag))
        tooltip_text_view->get_window(Gtk::TextWindowType::TEXT_WINDOW_TEXT)->set_cursor(link_mouse_cursor);
      else
        tooltip_text_view->get_window(Gtk::TextWindowType::TEXT_WINDOW_TEXT)->set_cursor(default_mouse_cursor);
      return false;
    });
    tooltip_text_view->signal_button_press_event().connect([this, tooltip_text_view](GdkEventButton *event) {
      if(event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
        Gtk::TextIter iter;
        int location_x, location_y;
        tooltip_text_view->window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, event->x, event->y, location_x, location_y);
        tooltip_text_view->get_iter_at_location(iter, location_x, location_y);
        if(iter.has_tag(link_tag)) {
          auto start = iter;
          if(!start.starts_tag(link_tag))
            start.backward_to_tag_toggle(link_tag);
          auto end = iter;
          end.forward_to_tag_toggle(link_tag);
          std::string text = tooltip_text_view->get_buffer()->get_text(start, end);
          std::string link;
          for(auto &tag : start.get_tags()) {
            auto it = links.find(tag);
            if(it != links.end()) {
              link = it->second;
              break;
            }
            it = reference_links.find(tag);
            if(it != reference_links.end()) {
              auto reference_it = references.find(it->second);
              if(reference_it != references.end())
                link = reference_it->second;
              break;
            }
          }
          if(link.empty())
            link = text;

          if(link.compare(0, 7, "http://") == 0 || link.compare(0, 8, "https://") == 0) {
            Notebook::get().open_uri(link);
            return true;
          }

          static std::regex regex("^([^:]+):([^:]+):([^:]+)$");
          std::smatch sm;
          if(std::regex_match(link, sm, regex)) {
            auto path = boost::filesystem::path(sm[1].str());
            if(auto view = dynamic_cast<Source::View *>(text_view))
              path = filesystem::get_normal_path(view->file_path.parent_path() / path);

            boost::system::error_code ec;
            if(boost::filesystem::is_regular_file(path, ec)) {
              Notebook::get().open(path);
              if(auto view = Notebook::get().get_current_view()) {
                try {
                  auto line = atoi(sm[2].str().c_str()) - 1;
                  auto offset = atoi(sm[3].str().c_str()) - 1;
                  view->place_cursor_at_line_offset(line, offset);
                  view->scroll_to_cursor_delayed(true, false);
                }
                catch(...) {
                }
              }
              return true;
            }
          }

          auto path = boost::filesystem::path(link);
          if(auto view = dynamic_cast<Source::View *>(text_view))
            path = filesystem::get_normal_path(view->file_path.parent_path() / path);
          boost::system::error_code ec;
          if(boost::filesystem::is_regular_file(path, ec)) {
            Notebook::get().open(path);
            return true;
          }
          Info::get().print("Could not open: " + link);
        }
      }
      return false;
    });

    auto layout = Pango::Layout::create(tooltip_text_view->get_pango_context());
    layout->set_text(buffer->get_text());
    layout->get_pixel_size(size.first, size.second);
    size.first += 6;  // 2xpadding
    size.second += 8; // 2xpadding + 2

    // Add ScrolledWindow if needed
    Gtk::Widget *widget = tooltip_text_view;
    auto screen_height = Gdk::Screen::get_default()->get_height();
    if(size.second > screen_height - 6 /* 2xpadding */) {
      auto scrolled_window = Gtk::manage(new Gtk::ScrolledWindow());
      scrolled_window->property_hscrollbar_policy() = Gtk::PolicyType::POLICY_NEVER;
      scrolled_window->property_vscrollbar_policy() = Gtk::PolicyType::POLICY_AUTOMATIC;
      scrolled_window->add(*tooltip_text_view);
      scrolled_window->set_size_request(-1, screen_height - 6 /* 2xpadding */);
      widget = scrolled_window;
    }

#if GTK_VERSION_GE(3, 20)
    box->add(*widget);
#else
    auto box2 = Gtk::manage(new Gtk::Box());
    box2->pack_start(*widget, true, true, 3);
    box->pack_start(*box2, true, true, 3);
#endif

    window->signal_realize().connect([this] {
      if(!text_view) {
        auto &dialog = SelectionDialog::get();
        if(dialog && dialog->is_visible()) {
          int root_x, root_y;
          dialog->get_position(root_x, root_y);
          root_x -= 3; // -1xpadding
          rectangle.set_x(root_x);
          rectangle.set_y(root_y - size.second);
          if(rectangle.get_y() < 0)
            rectangle.set_y(0);
        }
      }
      window->move(rectangle.get_x(), rectangle.get_y());
    });
  }

  if(buffer->size() == 0)
    return; // Do not show empty tooltips

  int root_x = 0, root_y = 0;
  if(text_view) {
    Gdk::Rectangle visible_rect;
    text_view->get_visible_rect(visible_rect);
    int visible_window_x, visible_window_y;
    text_view->buffer_to_window_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, visible_rect.get_x(), visible_rect.get_y(), visible_window_x, visible_window_y);

    auto window_x = std::max(activation_rectangle.get_x(), visible_window_x); // Adjust tooltip right if it is left of text_view
    auto window_y = activation_rectangle.get_y();
    text_view->get_window(Gtk::TextWindowType::TEXT_WINDOW_TEXT)->get_root_coords(window_x, window_y, root_x, root_y);
    root_x -= 3; // -1xpadding

    if(root_y < size.second)
      root_x += visible_rect.get_width() * 0.1;         // Adjust tooltip right if it might be above cursor
    rectangle.set_y(std::max(0, root_y - size.second)); // Move tooptip down if it is above screen

    // Move tooltip left if it is right of screen
    auto screen_width = Gdk::Screen::get_default()->get_width();
    rectangle.set_x(root_x + size.first > screen_width ? screen_width - size.first : root_x);
  }

  rectangle.set_width(size.first);
  rectangle.set_height(size.second);

  if(!disregard_drawn) {
    if(Tooltips::drawn_tooltips_rectangle.get_width() != 0) {
      if(rectangle.intersects(Tooltips::drawn_tooltips_rectangle)) {
        int new_y = Tooltips::drawn_tooltips_rectangle.get_y() - size.second;
        if(new_y >= 0)
          rectangle.set_y(new_y);
        else
          rectangle.set_x(Tooltips::drawn_tooltips_rectangle.get_x() + Tooltips::drawn_tooltips_rectangle.get_width() + 2);
      }
      Tooltips::drawn_tooltips_rectangle.join(rectangle);
    }
    else
      Tooltips::drawn_tooltips_rectangle = rectangle;
  }

  if(window->get_realized())
    window->move(rectangle.get_x(), rectangle.get_y());
  window->show_all();
  shown = true;
}

void Tooltip::hide(const std::pair<int, int> &last_mouse_pos, const std::pair<int, int> &mouse_pos) {
  // Keep tooltip if mouse is moving towards it
  // Calculated using dot product between the mouse_pos vector and the corners of the tooltip window
  if(text_view && window && shown && last_mouse_pos.first != -1 && last_mouse_pos.second != -1 && mouse_pos.first != -1 && mouse_pos.second != -1) {
    static int root_x, root_y;
    text_view->get_window(Gtk::TextWindowType::TEXT_WINDOW_TEXT)->get_root_coords(last_mouse_pos.first, last_mouse_pos.second, root_x, root_y);
    int diff_x = mouse_pos.first - last_mouse_pos.first;
    int diff_y = mouse_pos.second - last_mouse_pos.second;
    class Corner {
    public:
      Corner(int x, int y) : x(x - root_x), y(y - root_y) {}
      int x, y;
    };
    std::vector<Corner> corners;
    corners.emplace_back(rectangle.get_x(), rectangle.get_y());
    corners.emplace_back(rectangle.get_x() + rectangle.get_width(), rectangle.get_y());
    corners.emplace_back(rectangle.get_x(), rectangle.get_y() + rectangle.get_height());
    corners.emplace_back(rectangle.get_x() + rectangle.get_width(), rectangle.get_y() + rectangle.get_height());
    for(auto &corner : corners) {
      if(diff_x * corner.x + diff_y * corner.y >= 0)
        return;
    }
  }
  Tooltips::shown_tooltips.erase(this);
  if(window)
    window->hide();
  shown = false;
}

void Tooltip::wrap_lines() {
  auto iter = buffer->begin();
  if(!iter)
    return;

  while(true) {
    auto last_space = buffer->end();
    bool long_line = true;
    for(unsigned c = 0; c <= 80; c++) {
      if(*iter == ' ')
        last_space = iter;
      if(iter.ends_line()) {
        long_line = false;
        break;
      }
      if(!iter.forward_char())
        return;
    }
    if(long_line) {
      if(!last_space) { // If word is longer than 80
        while(true) {
          if(iter.ends_line())
            break;
          if(*iter == ' ') {
            last_space = iter;
            break;
          }
          if(!iter.forward_char())
            return;
        }
      }
      if(last_space) {
        auto mark = buffer->create_mark(last_space);

        auto next = last_space;
        next.forward_char();
        buffer->erase(last_space, next);
        buffer->insert(mark->get_iter(), "\n");

        iter = mark->get_iter();
        buffer->delete_mark(mark);
      }
    }
    if(!iter.forward_char())
      return;
  }
}

void Tooltip::insert_with_links_tagged(const std::string &text) {
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

void Tooltip::insert_markdown(const std::string &input) {
  if(!h1_tag) {
    h1_tag = buffer->create_tag();
    h1_tag->property_weight() = Pango::WEIGHT_ULTRAHEAVY;
    h1_tag->property_underline() = Pango::UNDERLINE_DOUBLE;

    h2_tag = buffer->create_tag();
    h2_tag->property_weight() = Pango::WEIGHT_ULTRAHEAVY;
    h2_tag->property_underline() = Pango::UNDERLINE_SINGLE;

    h3_tag = buffer->create_tag();
    h3_tag->property_weight() = Pango::WEIGHT_ULTRAHEAVY;

    auto source_font_family = Pango::FontDescription(Config::get().source.font).get_family();

    code_tag = buffer->create_tag();
    code_tag->property_family() = source_font_family;
    auto background_rgba = Gdk::RGBA();
    auto normal_color = window->get_style_context()->get_color(Gtk::StateFlags::STATE_FLAG_NORMAL);
    auto light_theme = (normal_color.get_red() + normal_color.get_green() + normal_color.get_blue()) / 3 < 0.5;
    if(light_theme)
      background_rgba.set_rgba(1.0, 1.0, 1.0, 0.4);
    else
      background_rgba.set_rgba(0.0, 0.0, 0.0, 0.2);
    code_tag->property_background_rgba() = background_rgba;

    code_block_tag = buffer->create_tag();
    code_block_tag->property_family() = source_font_family;
    code_block_tag->property_paragraph_background_rgba() = background_rgba;

    bold_tag = buffer->create_tag();
    bold_tag->property_weight() = Pango::WEIGHT_ULTRAHEAVY;

    italic_tag = buffer->create_tag();
    italic_tag->property_style() = Pango::Style::STYLE_ITALIC;

    strikethrough_tag = buffer->create_tag();
    strikethrough_tag->property_strikethrough() = true;
  }

  size_t i = 0;

  auto forward_to = [&](const std::vector<char> chars) {
    for(; i < input.size(); i++) {
      if(std::any_of(chars.begin(), chars.end(), [&input, &i](char chr) { return input[i] == chr; }))
        return true;
    }
    return false;
  };

  auto forward_passed = [&](const std::vector<char> chars) {
    for(; i < input.size(); i++) {
      if(std::none_of(chars.begin(), chars.end(), [&input, &i](char chr) { return input[i] == chr; }))
        return true;
    }
    return false;
  };

  auto unescape = [&](size_t &i) {
    if(input[i] == '\\') {
      if(i + 1 < input.size())
        i++;
      return true;
    }
    return false;
  };

  std::function<void(size_t, size_t)> insert_text = [&](size_t from, size_t to) {
    auto i = from;

    std::string partial;

    auto is_whitespace_character = [&](size_t i) {
      return input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r' || input[i] == '\f';
    };

    auto insert_emphasis = [&] {
      if(i > from && !is_whitespace_character(i - 1)) // Do not emphasis: normal_text
        return false;
      auto i_saved = i;
      std::string prefix;
      for(; i < to && prefix.size() < 3 && (input[i] == '*' || input[i] == '_'); i++)
        prefix += input[i];
      if(prefix.empty())
        return false;
      if(prefix.size() == 2)
        std::swap(prefix[0], prefix[1]); // To match: *_test_*
      else if(prefix.size() == 3)
        std::swap(prefix[0], prefix[2]);         // To match: **_test_**
      if(i < to && is_whitespace_character(i)) { // Do not emphasis for instance: 2 * 2 * 2
        i = i_saved;
        return false;
      }
      insert_with_links_tagged(partial);
      partial.clear();
      auto start = i;
      for(; i < to; i++) {
        if(!unescape(i)) {
          if(input.compare(i, prefix.size(), prefix) == 0) {
            if(i - 1 > from && is_whitespace_character(i - 1)) { // Do not emphasis _test in: _test _test_
              i = i_saved;
              return false;
            }
            if(i + prefix.size() < to && !is_whitespace_character(i + prefix.size())) // Emphasis italic_text in: _italic_text_
              continue;
            break;
          }
        }
      }
      if(i == to) {
        i = i_saved;
        return false;
      }
      auto start_offset = buffer->get_insert()->get_iter().get_offset();
      insert_text(start, i);
      if(prefix.size() == 1)
        buffer->apply_tag(italic_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
      else if(prefix.size() == 2)
        buffer->apply_tag(bold_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
      else {
        buffer->apply_tag(italic_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
        buffer->apply_tag(bold_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
      }
      i += prefix.size() - 1;
      return true;
    };

    auto insert_strikethrough = [&] {
      if(input.compare(i, 2, "~~") == 0) {
        insert_with_links_tagged(partial);
        partial.clear();
        auto i_saved = i;
        i += 2;
        if(i < to) {
          auto start = i;
          for(; i < to; i++) {
            if(!unescape(i) && input.compare(i, 2, "~~") == 0)
              break;
          }
          if(i == to) {
            i = i_saved;
            return false;
          }
          auto start_offset = buffer->get_insert()->get_iter().get_offset();
          insert_text(start, i);
          buffer->apply_tag(strikethrough_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
          i++;
          return true;
        }
        i = i_saved;
      }
      return false;
    };

    auto insert_code = [&] {
      if(input[i] == '`') {
        insert_with_links_tagged(partial);
        partial.clear();
        auto i_saved = i;
        i++;
        if(i < to) {
          bool escaped = false;
          if(input[i] == '`') {
            escaped = true;
            i++;
          }
          if(i < to) {
            auto start = i;
            for(; i < to; i++) {
              if(input[i] == '`') {
                if(!escaped)
                  break;
                if(i + 1 < to && input[i + 1] == '`')
                  break;
              }
            }
            if(i == to) {
              i = i_saved;
              return false;
            }
            buffer->insert_with_tag(buffer->get_insert()->get_iter(), input.substr(start, i - start), code_tag);
            if(escaped)
              i++;
            return true;
          }
        }
        i = i_saved;
      }
      return false;
    };

    auto insert_link = [&] {
      if(input[i] == '[') {
        insert_with_links_tagged(partial);
        partial.clear();
        auto i_saved = i;
        i++;
        if(i < to) {
          auto text_start = i;
          for(; i < to; i++) {
            if(!unescape(i) && input[i] == ']')
              break;
          }
          if(i == to) {
            i = i_saved;
            return false;
          }
          auto text_end = i;
          i++;
          if(i < to && input[i] == '(') {
            i++;
            auto link_start = i;
            for(; i < to; i++) {
              if(!unescape(i) && input[i] == ')')
                break;
            }
            if(i == to) {
              i = i_saved;
              return false;
            }
            auto start_offset = buffer->get_insert()->get_iter().get_offset();
            insert_text(text_start, text_end);
            auto start = buffer->get_iter_at_offset(start_offset);
            auto end = buffer->get_insert()->get_iter();
            buffer->apply_tag(link_tag, start, end);
            auto tag = buffer->create_tag();
            buffer->apply_tag(tag, start, end);
            links.emplace(tag, input.substr(link_start, i - link_start));
            return true;
          }
          else if(i < to && input[i] == '[') {
            i++;
            auto link_start = i;
            for(; i < to; i++) {
              if(!unescape(i) && input[i] == ']')
                break;
            }
            if(i == to) {
              i = i_saved;
              return false;
            }
            auto start_offset = buffer->get_insert()->get_iter().get_offset();
            insert_text(text_start, text_end);
            auto start = buffer->get_iter_at_offset(start_offset);
            auto end = buffer->get_insert()->get_iter();
            buffer->apply_tag(link_tag, start, end);
            auto tag = buffer->create_tag();
            buffer->apply_tag(tag, start, end);
            reference_links.emplace(tag, input.substr(link_start, i - link_start));
            return true;
          }
          else {
            auto start_offset = buffer->get_insert()->get_iter().get_offset();
            insert_text(text_start, text_end);
            auto start = buffer->get_iter_at_offset(start_offset);
            auto end = buffer->get_insert()->get_iter();
            buffer->apply_tag(link_tag, start, end);
            auto tag = buffer->create_tag();
            buffer->apply_tag(tag, start, end);
            reference_links.emplace(tag, buffer->get_text(start, end));
            i = text_end;
            return true;
          }
        }
        i = i_saved;
      }
      return false;
    };

    for(; i < to; i++) {
      if(!unescape(i) && (insert_code() || insert_emphasis() || insert_strikethrough() || insert_link()))
        continue;
      partial += input[i];
    }
    insert_with_links_tagged(partial);
  };

  auto insert_header = [&] {
    size_t end_next_line = std::string::npos;
    int header = 0;
    for(; i < input.size() && header < 6 && input[i] == '#'; i++)
      header++;
    if(header == 0) {
      auto i_saved = i;
      forward_to({'\n'});
      i++;
      if(i < input.size() && (input[i] == '=' || input[i] == '-')) {
        forward_passed({input[i]});
        if(i == input.size() || input[i] == '\n') {
          if(input[i - 1] == '=')
            header = 1;
          else
            header = 2;
          end_next_line = i;
        }
      }
      i = i_saved;
    }
    if(header == 0)
      return false;
    forward_passed({' '});
    auto start = i;
    forward_to({'\n'});
    auto end = buffer->end();
    if(end.backward_char() && !end.starts_line())
      buffer->insert_at_cursor("\n");
    auto start_offset = buffer->get_insert()->get_iter().get_offset();
    insert_text(start, i);
    if(header == 1)
      buffer->apply_tag(h1_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
    else if(header == 2)
      buffer->apply_tag(h2_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
    else if(header == 3)
      buffer->apply_tag(h3_tag, buffer->get_iter_at_offset(start_offset), buffer->get_insert()->get_iter());
    buffer->insert_at_cursor("\n\n");
    if(end_next_line != std::string::npos)
      i = end_next_line;
    return true;
  };


  auto insert_code_block = [&] {
    if(input.compare(i, 3, "```") == 0) {
      auto i_saved = i;
      if(forward_to({'\n'})) {
        i++;
        if(i < input.size()) {
          auto start = i;
          while(i < input.size() && !(input[i - 1] == '\n' && input.compare(i, 3, "```") == 0))
            i++;
          if(i == input.size()) {
            i = i_saved;
            return false;
          }
          auto end = buffer->end();
          if(end.backward_char() && !end.starts_line())
            buffer->insert_at_cursor("\n");
          buffer->insert_with_tag(buffer->get_insert()->get_iter(), input.substr(start, i - start), code_block_tag);
          buffer->insert_at_cursor("\n");
          i += 3;
          return true;
        }
      }
      i = i_saved;
    }
    return false;
  };

  auto insert_reference = [&] {
    if(input[i] == '[') {
      auto i_saved = i;
      i++;
      if(i < input.size()) {
        auto reference_start = i;
        for(; i < input.size(); i++) {
          if(!unescape(i) && input[i] == ']')
            break;
        }
        if(i == input.size()) {
          i = i_saved;
          return false;
        }
        auto reference_end = i;
        i++;
        if(forward_passed({' ', '\n'}) && input[i] == ':') {
          i++;
          if(forward_passed({' ', '\n'})) {
            auto link_start = i;
            forward_to({' ', '\n'});
            auto start_offset = buffer->get_insert()->get_iter().get_offset();
            insert_text(reference_start, reference_end);
            auto start = buffer->get_iter_at_offset(start_offset);
            auto end = buffer->get_insert()->get_iter();
            references.emplace(buffer->get_text(start, end), input.substr(link_start, i - link_start));
            buffer->erase(start, end);
            return true;
          }
        }
      }
      i = i_saved;
    }
    return false;
  };

  while(forward_passed({'\n'})) {
    if(insert_header() || insert_code_block() || insert_reference())
      continue;
    // Insert paragraph:
    auto start = i;
    for(; forward_to({'\n'}); i++) {
      if(i + 1 < input.size() && (input[i + 1] == '\n' || input[i + 1] == '#' || input.compare(i + 1, 3, "```") == 0))
        break;
    }
    insert_text(start, i);
    if(i < input.size())
      buffer->insert_at_cursor("\n\n");
  }

  remove_trailing_newlines();
}

void Tooltip::remove_trailing_newlines() {
  auto end = buffer->end();
  while(end.starts_line() && end.backward_char()) {
  }
  buffer->erase(end, buffer->end());
}

void Tooltips::show(const Gdk::Rectangle &rectangle, bool disregard_drawn) {
  for(auto &tooltip : tooltip_list) {
    tooltip.update();
    if(rectangle.intersects(tooltip.activation_rectangle))
      tooltip.show(disregard_drawn, on_motion);
    else
      tooltip.hide();
  }
}

void Tooltips::show(bool disregard_drawn) {
  for(auto &tooltip : tooltip_list) {
    tooltip.update();
    tooltip.show(disregard_drawn, on_motion);
  }
}

void Tooltips::hide(const std::pair<int, int> &last_mouse_pos, const std::pair<int, int> &mouse_pos) {
  for(auto &tooltip : tooltip_list)
    tooltip.hide(last_mouse_pos, mouse_pos);
}
