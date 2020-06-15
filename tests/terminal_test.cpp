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

  {
    auto link = Terminal::get().find_link("~/test/test.cc:7:41: error: expected ';' after expression.");
    assert(link);
    assert(link->start_pos == 0);
    assert(link->end_pos == 19);
    assert(link->path == "~/test/test.cc");
    assert(link->line == 7);
    assert(link->line_index == 41);
  }
  {
    auto link = Terminal::get().find_link("Assertion failed: (false), function main, file ~/test/test.cc, line 15.");
    assert(link);
    assert(link->start_pos == 47);
    assert(link->end_pos == 70);
    assert(link->path == "~/test/test.cc");
    assert(link->line == 15);
    assert(link->line_index == 1);
  }
  {
    auto link = Terminal::get().find_link("test: ~/examples/main.cpp:17: int main(int, char**): Assertion `false' failed.");
    assert(link);
    assert(link->start_pos == 6);
    assert(link->end_pos == 28);
    assert(link->path == "~/examples/main.cpp");
    assert(link->line == 17);
    assert(link->line_index == 1);
  }
  {
    auto link = Terminal::get().find_link("ERROR:~/test/test.cc:36:int main(): assertion failed: (false)");
    assert(link);
    assert(link->start_pos == 6);
    assert(link->end_pos == 23);
    assert(link->path == "~/test/test.cc");
    assert(link->line == 36);
    assert(link->line_index == 1);
  }
}
