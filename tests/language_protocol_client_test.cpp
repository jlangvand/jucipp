#include "config.hpp"
#include "source_language_protocol.hpp"
#include <glib.h>

//Requires display server to work
//However, it is possible to use the Broadway backend if the test is run in a pure terminal environment:
//broadwayd&
//make test

void flush_events() {
  while(Gtk::Main::events_pending())
    Gtk::Main::iteration();
}

int main() {
  auto app = Gtk::Application::create();
  Gsv::init();

  auto tests_path = boost::filesystem::canonical(JUCI_TESTS_PATH);
  auto build_path = boost::filesystem::canonical(JUCI_BUILD_PATH);

  auto view = new Source::LanguageProtocolView(boost::filesystem::canonical(tests_path / "language_protocol_test_files" / "main.rs"),
                                               Source::LanguageManager::get_default()->get_language("rust"),
                                               "rust",
                                               (build_path / "tests" / "language_protocol_server_test").string());

  while(!view->initialized)
    flush_events();

  g_assert(view->capabilities.document_formatting);

  view->get_buffer()->insert_at_cursor(" ");
  g_assert(view->get_buffer()->get_text() == R"( fn main() {
    println!("Hello, world!");
}
)");

  view->format_style(false);
  g_assert(view->get_buffer()->get_text() == R"(fn main() {
    println!("Hello, world!");
}
)");

  std::atomic<int> exit_status(-1);
  view->client->on_exit_status = [&exit_status](int exit_status_) {
    exit_status = exit_status_;
  };

  delete view;

  while(exit_status == -1)
    flush_events();

  g_assert_cmpint(exit_status, ==, 0);
}
