#include "terminal.hpp"
#include "test_suite.h"

int main() {
  const auto test_directory = "Terminal_tests";
  {
    suite test_suite(test_directory);
    {
      auto &terminal = Terminal::get();
      auto connection = terminal.get_buffer()->signal_insert().connect([&](const Gtk::TextBuffer::iterator &, const Glib::ustring &msg, int) {
        g_assert_cmpstr(msg.c_str(), ==, "Hello, World!\n");
        test_suite.has_assertion = true;
      });
      auto module = py::module::import("terminal_test");
      module.attr("hello_world")();
      connection.disconnect();
    }
  }
  {
    suite test_suite(test_directory);
    {
      auto &terminal = Terminal::get();
      auto connection = terminal.get_buffer()->signal_insert().connect([&](const Gtk::TextBuffer::iterator &, const Glib::ustring &msg, int) {
        g_assert_cmpstr(msg.c_str(), ==, "hello_world.txt\n");
        test_suite.has_assertion = true;
        test_suite.app->release();
      });
      test_suite.app->hold();
      std::thread thread([&] {
        const auto ls_dir = test_suite.test_file_path / test_directory / "ls";
        auto module = py::module::import("terminal_test");
        auto res = module.attr("process")(ls_dir).cast<int>();
        g_assert_cmpint(res, ==, 0);
      });
      test_suite.app->run();
      thread.join();
      connection.disconnect();
    }
  }
}