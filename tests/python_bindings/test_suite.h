#pragma once
#include "config.hpp"
#include "plugins.h"
#include <gtkmm.h>

class __attribute__((visibility("default")))
suite {
public:
  suite(const boost::filesystem::path &path);
  Glib::RefPtr<Gtk::Application> app = Gtk::Application::create();
  Config &config = Config::get();
  boost::filesystem::path test_file_path = boost::filesystem::canonical(std::string(JUCI_TESTS_PATH) + "/python_bindings");
  boost::filesystem::path build_file_path = boost::filesystem::canonical(JUCI_BUILD_PATH);
  bool has_assertion = false;
  Plugins plugins;
  ~suite();
};
