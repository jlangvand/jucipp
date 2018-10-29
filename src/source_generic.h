#pragma once

#include "autocomplete.h"
#include "source.h"
#include <atomic>

namespace Source {
  class GenericView : public View {
  public:
    GenericView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language);
    ~GenericView();

  private:
    void parse_language_file(bool &has_context_class, const boost::property_tree::ptree &pt);

    std::set<std::string> keywords;

    bool is_word_iter(const Gtk::TextIter &iter);
    std::pair<Gtk::TextIter, Gtk::TextIter> get_word(Gtk::TextIter iter);
    std::vector<std::pair<Gtk::TextIter, Gtk::TextIter>> get_words(const Gtk::TextIter &start, const Gtk::TextIter &end);

    std::map<std::string, size_t> buffer_words;
    void setup_buffer_words();
    std::atomic<bool> show_prefix_buffer_word = {false}; /// To avoid showing the current word if it is unique in document

    Autocomplete autocomplete;
    std::vector<std::string> autocomplete_comment;
    std::vector<std::string> autocomplete_insert;
    void setup_autocomplete();
  };
} // namespace Source
