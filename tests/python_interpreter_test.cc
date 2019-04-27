#include "config.h"
#include "plugins.h"
#include "python_interpreter.h"
#include "terminal.h"
#include "config.h"
#include "python_type_casters.h"


class __attribute__((visibility("default")))
suite {
public:
  Glib::RefPtr<Gtk::Application> app = Gtk::Application::create();
  Plugins plugins;
  Terminal &terminal = Terminal::get();
  Config &config = Config::get();
  boost::filesystem::path test_file_path = boost::filesystem::canonical(std::string(JUCI_TESTS_PATH) + "/python_interpreter_test_files");
  bool has_assertion = false;
  suite() {
    auto sys = plugins.interpreter.add_module("sys");
    sys.attr("path").cast<py::list>().append(test_file_path.string());
    config.terminal.history_size = 100;
  }
  ~suite() {
    g_assert_true(has_assertion);
  }
};

int main() {
  {
    suite test_suite;
    {
      py::module::import("basic_test");
      try {
        py::module::import("exception_test");
      }
      catch(const py::error_already_set &error) {
        test_suite.has_assertion = true;
      }
    }
  }

  {
    suite test_suite;
    {
      auto connection = test_suite.terminal.get_buffer()->signal_insert().connect([&](const Gtk::TextBuffer::iterator &, const Glib::ustring &msg, int) {
        g_assert_cmpstr(msg.c_str(), ==, "Hello, World!\n");
        test_suite.has_assertion = true;
      });
      auto module = py::module::import("terminal_test");
      module.attr("hello_world")();
      connection.disconnect();
    }
  }
  {
    suite test_suite;
    {
      auto connection = test_suite.terminal.get_buffer()->signal_insert().connect([&](const Gtk::TextBuffer::iterator &, const Glib::ustring &msg, int) {
        g_assert_cmpstr(msg.c_str(), ==, "hello_world.txt\n");
        test_suite.has_assertion = true;
        test_suite.app->release();
      });
      test_suite.app->hold();
      std::thread thread([&] {
        const auto ls_dir = test_suite.test_file_path / "ls";
        auto module = py::module::import("terminal_test");
        auto res = module.attr("process")(ls_dir).cast<int>();
        g_assert_cmpint(res, ==, 0);
      });
      test_suite.app->run();
      thread.join();
      connection.disconnect();
    }
  }

  return 0;
}
