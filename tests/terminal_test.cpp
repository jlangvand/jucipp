#include "config.hpp"
#include "terminal.hpp"
#include <glib.h>
#include <gtksourceviewmm.h>

//Requires display server to work
//However, it is possible to use the Broadway backend if the test is run in a pure terminal environment:
//broadwayd&
//make test

int main() {
  auto app = Gtk::Application::create();
  Gsv::init();
  auto &terminal = Terminal::get();
  auto buffer = terminal.get_buffer();

  {
    Config::get().terminal.history_size = 1;
    terminal.print("test");
    assert(buffer->get_text() == "test");
    terminal.print("\ntest2");
    assert(buffer->get_text() == "test2");
    Config::get().terminal.history_size = 10000;
  }

  // Testing links
  {
    auto link = terminal.find_link("~/test/test.cc:7:41: error: expected ';' after expression.");
    assert(link);
    assert(link->start_pos == 0);
    assert(link->end_pos == 19);
    assert(link->path == "~/test/test.cc");
    assert(link->line == 7);
    assert(link->line_index == 41);
  }
  {
    auto link = terminal.find_link("Assertion failed: (false), function main, file ~/test/test.cc, line 15.");
    assert(link);
    assert(link->start_pos == 47);
    assert(link->end_pos == 70);
    assert(link->path == "~/test/test.cc");
    assert(link->line == 15);
    assert(link->line_index == 1);
  }
  {
    auto link = terminal.find_link("test: ~/examples/main.cpp:17: int main(int, char**): Assertion `false' failed.");
    assert(link);
    assert(link->start_pos == 6);
    assert(link->end_pos == 28);
    assert(link->path == "~/examples/main.cpp");
    assert(link->line == 17);
    assert(link->line_index == 1);
  }
  {
    auto link = terminal.find_link("ERROR:~/test/test.cc:36:int main(): assertion failed: (false)");
    assert(link);
    assert(link->start_pos == 6);
    assert(link->end_pos == 23);
    assert(link->path == "~/test/test.cc");
    assert(link->line == 36);
    assert(link->line_index == 1);
  }
  {
    auto link = terminal.find_link("  --> src/main.rs:16:4");
    assert(link);
    assert(link->start_pos == 6);
    assert(link->end_pos == 22);
    assert(link->path == "src/main.rs");
    assert(link->line == 16);
    assert(link->line_index == 4);
  }
  {
    auto link = terminal.find_link(R"(  File "/home/test/test.py", line 4, in <module>)");
    assert(link);
    assert(link->start_pos == 8);
    assert(link->end_pos == 35);
    assert(link->path == "/home/test/test.py");
    assert(link->line == 4);
    assert(link->line_index == 1);
  }

  // Testing print
  {
    terminal.clear();
    terminal.print("test");
    assert(buffer->get_text() == "test");
    auto iter = buffer->begin();
    assert(!(iter.has_tag(terminal.bold_tag) || iter.forward_to_tag_toggle(terminal.bold_tag)));

    terminal.clear();
    terminal.print("test", true);
    assert(buffer->get_text() == "test");
    iter = buffer->begin();
    assert(iter.begins_tag(terminal.bold_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.bold_tag));
  }
  {
    terminal.clear();
    terminal.print("te\xff\xff\xffst");
    assert(buffer->get_text() == "te???st");
  }
  {
    terminal.clear();
    terminal.print("~/test/test.cc:7:41: error: expected ';' after expression.\n");
    assert(buffer->get_text() == "~/test/test.cc:7:41: error: expected ';' after expression.\n");
    auto iter = buffer->begin();
    assert(iter.begins_tag(terminal.link_tag));
    iter.forward_chars(19);
    assert(iter.ends_tag(terminal.link_tag));
  }
  {
    terminal.clear();
    terminal.print("~/test/test.cc:7:41: error: ");
    terminal.print("expected ';' after expression.\n");
    assert(buffer->get_text() == "~/test/test.cc:7:41: error: expected ';' after expression.\n");
    auto iter = buffer->begin();
    assert(iter.begins_tag(terminal.link_tag));
    iter.forward_chars(19);
    assert(iter.ends_tag(terminal.link_tag));
  }

  // Testing ansi colors
  {
    terminal.clear();
    terminal.print("\e[31mtest\e[m\e[32mtest\e[m\e[33mtest\e[m\e[34mtest\e[m\e[35mtest\e[m\e[36mtest\e[m");
    assert(buffer->get_text(true) == "\e[31mtest\e[m\e[32mtest\e[m\e[33mtest\e[m\e[34mtest\e[m\e[35mtest\e[m\e[36mtest\e[m");
    assert(buffer->get_text(false) == "testtesttesttesttesttest");
    auto iter = buffer->begin();
    iter.forward_chars(5);
    assert(iter.starts_tag(terminal.red_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.red_tag));
    iter.forward_chars(8);
    assert(iter.starts_tag(terminal.green_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.green_tag));
    iter.forward_chars(8);
    assert(iter.starts_tag(terminal.yellow_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.yellow_tag));
    iter.forward_chars(8);
    assert(iter.starts_tag(terminal.blue_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.blue_tag));
    iter.forward_chars(8);
    assert(iter.starts_tag(terminal.magenta_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.magenta_tag));
    iter.forward_chars(8);
    assert(iter.starts_tag(terminal.cyan_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.cyan_tag));
  }
  {
    terminal.clear();
    terminal.print("\e[31mte");
    terminal.print("st\e[m");
    assert(buffer->get_text(true) == "\e[31mtest\e[m");
    assert(buffer->get_text(false) == "test");
    auto iter = buffer->begin();
    iter.forward_chars(5);
    assert(iter.starts_tag(terminal.red_tag));
    iter.forward_chars(4);
    assert(iter.ends_tag(terminal.red_tag));
  }
  {
    terminal.clear();
    terminal.print("enable_\e[01;31m\e[Ktest\e[m\e[King");
    assert(buffer->get_text(true) == "enable_\e[01;31m\e[Ktest\e[m\e[King");
    assert(buffer->get_text(false) == "enable_testing");
    auto iter = buffer->begin();
    iter.forward_chars(15);
    assert(iter.starts_tag(terminal.red_tag));
    iter.forward_chars(7);
    assert(iter.ends_tag(terminal.red_tag));
  }
  {
    terminal.clear();
    terminal.print("enable_\e[01;31m\e[Ktest\e[0m\e[King");
    assert(buffer->get_text(true) == "enable_\e[01;31m\e[Ktest\e[0m\e[King");
    assert(buffer->get_text(false) == "enable_testing");
    auto iter = buffer->begin();
    iter.forward_chars(15);
    assert(iter.starts_tag(terminal.red_tag));
    iter.forward_chars(7);
    assert(iter.ends_tag(terminal.red_tag));
  }
  {
    terminal.clear();
    terminal.print("test\e[0m\e[7m\e[1m\e[31mtest\e[39m\e[22m\e[27m\e[0mtest");
    assert(buffer->get_text(true) == "test\e[0m\e[7m\e[1m\e[31mtest\e[39m\e[22m\e[27m\e[0mtest");
    assert(buffer->get_text(false) == "testtesttest");
    auto iter = buffer->begin();
    assert(iter.get_tags().empty());
    iter.forward_visible_cursor_positions(3);
    assert(iter.get_tags().empty());
    iter.forward_visible_cursor_positions(1);
    assert(iter.get_tags() == std::vector<Glib::RefPtr<Gtk::TextTag>>{terminal.red_tag});
    iter.forward_visible_cursor_positions(3);
    assert(iter.get_tags() == std::vector<Glib::RefPtr<Gtk::TextTag>>{terminal.red_tag});
    iter.forward_visible_cursor_positions(1);
    assert(iter.get_tags().empty());
  }
  {
    terminal.clear();
    terminal.print("\e[31mtest\e[39mtest");
    assert(buffer->get_text(true) == "\e[31mtest\e[39mtest");
    assert(buffer->get_text(false) == "testtest");
    auto iter = buffer->begin();
    iter.forward_visible_cursor_position();
    assert(iter.starts_tag(terminal.red_tag));
    iter.forward_visible_cursor_positions(3);
    assert(iter.get_tags() == std::vector<Glib::RefPtr<Gtk::TextTag>>{terminal.red_tag});
    iter.forward_visible_cursor_positions(1);
    assert(iter.get_tags().empty());
  }
}
