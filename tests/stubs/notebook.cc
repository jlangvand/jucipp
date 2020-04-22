#include "notebook.h"

Notebook::Notebook() {}

Source::View *Notebook::get_current_view() {
  return nullptr;
}

void Notebook::open(const boost::filesystem::path &file_path, Position position) {}

void Notebook::open_uri(const std::string &uri) {}
