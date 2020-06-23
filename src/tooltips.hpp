#pragma once
#include <boost/optional.hpp>
#include <functional>
#include <gtkmm.h>
#include <list>
#include <set>
#include <string>
#include <unordered_map>

class Tooltip {
public:
  Tooltip(Gtk::TextView *text_view, const Gtk::TextIter &start_iter, const Gtk::TextIter &end_iter, std::function<void(Tooltip &)> set_buffer_);
  Tooltip(std::function<void(Tooltip &tooltip)> set_buffer_);
  ~Tooltip();

  void update();
  void show(bool disregard_drawn = false, const std::function<void()> &on_motion = nullptr);
  void hide(const boost::optional<std::pair<int, int>> &last_mouse_pos, const boost::optional<std::pair<int, int>> &mouse_pos);

  Gdk::Rectangle activation_rectangle;
  Glib::RefPtr<Gtk::TextBuffer::Mark> start_mark;
  Glib::RefPtr<Gtk::TextBuffer::Mark> end_mark;

  Glib::RefPtr<Gtk::TextBuffer> buffer;

  void insert_with_links_tagged(const std::string &text);
  void insert_markdown(const std::string &text);
  // Remove empty lines at end of buffer
  void remove_trailing_newlines();

private:
  std::unique_ptr<Gtk::Window> window;
  void wrap_lines();

  Gtk::TextView *text_view;
  std::function<void(Tooltip &)> set_buffer;
  std::pair<int, int> size;
  Gdk::Rectangle rectangle;

  bool shown = false;

  Glib::RefPtr<Gtk::TextTag> link_tag;
  Glib::RefPtr<Gtk::TextTag> h1_tag;
  Glib::RefPtr<Gtk::TextTag> h2_tag;
  Glib::RefPtr<Gtk::TextTag> h3_tag;
  Glib::RefPtr<Gtk::TextTag> code_tag;
  Glib::RefPtr<Gtk::TextTag> code_block_tag;
  Glib::RefPtr<Gtk::TextTag> bold_tag;
  Glib::RefPtr<Gtk::TextTag> italic_tag;
  Glib::RefPtr<Gtk::TextTag> strikethrough_tag;

  std::map<Glib::RefPtr<Gtk::TextTag>, std::string> links;
  std::map<Glib::RefPtr<Gtk::TextTag>, std::string> reference_links;
  std::unordered_map<std::string, std::string> references;
};

class Tooltips {
public:
  static std::set<Tooltip *> shown_tooltips;
  static Gdk::Rectangle drawn_tooltips_rectangle;
  static void init() { drawn_tooltips_rectangle = Gdk::Rectangle(); }

  void show(const Gdk::Rectangle &rectangle, bool disregard_drawn = false);
  void show(bool disregard_drawn = false);
  void hide(const boost::optional<std::pair<int, int>> &last_mouse_pos = {}, const boost::optional<std::pair<int, int>> &mouse_pos = {});
  void clear() { tooltip_list.clear(); };

  template <typename... Ts>
  void emplace_back(Ts &&... params) {
    tooltip_list.emplace_back(std::forward<Ts>(params)...);
  }

  std::function<void()> on_motion;

private:
  std::list<Tooltip> tooltip_list;
};
