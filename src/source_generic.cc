#include "source_generic.h"
#include "ctags.h"
#include "filesystem.h"
#include "info.h"
#include "selection_dialog.h"
#include "snippets.h"
#include "terminal.h"
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
inline DWORD get_current_process_id() {
  return GetCurrentProcessId();
}
#else
#include <unistd.h>
inline pid_t get_current_process_id() {
  return getpid();
}
#endif

Source::GenericView::GenericView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language) : BaseView(file_path, language), View(file_path, language, true), autocomplete(this, interactive_completion, last_keyval, false) {
  configure();
  spellcheck_all = true;

  if(language) {
    auto language_manager = LanguageManager::get_default();
    auto search_paths = language_manager->get_search_path();
    bool found_language_file = false;
    boost::filesystem::path language_file;
    for(auto &search_path : search_paths) {
      boost::filesystem::path p(static_cast<std::string>(search_path) + '/' + static_cast<std::string>(language->get_id()) + ".lang");
      if(boost::filesystem::exists(p) && boost::filesystem::is_regular_file(p)) {
        language_file = p;
        found_language_file = true;
        break;
      }
    }

    if(found_language_file) {
      boost::property_tree::ptree pt;
      try {
        boost::property_tree::xml_parser::read_xml(language_file.string(), pt);
      }
      catch(const std::exception &e) {
        Terminal::get().print("Error: error parsing language file " + language_file.string() + ": " + e.what() + '\n', true);
      }
      bool has_context_class = false;
      parse_language_file(has_context_class, pt);
      if(!has_context_class || language->get_id() == "cmake") // TODO: no longer use the spellcheck_all flag?
        spellcheck_all = false;
    }
  }

  setup_buffer_words();

  setup_autocomplete();

  get_methods = [this]() {
    std::vector<std::pair<Offset, std::string>> methods;
    boost::filesystem::path file_path;
    boost::system::error_code ec;
    bool use_tmp_file = false;

    if(this->get_buffer()->get_modified()) {
      use_tmp_file = true;
      file_path = boost::filesystem::temp_directory_path(ec);
      if(ec) {
        Terminal::get().print("Error: could not get temporary directory folder\n", true);
        return methods;
      }
      file_path /= ("jucipp_get_methods" + std::to_string(get_current_process_id()) + this->file_path.filename().string());
      filesystem::write(file_path, this->get_buffer()->get_text().raw());
    }
    else
      file_path = this->file_path;

    auto pair = Ctags::get_result(file_path);
    if(use_tmp_file)
      boost::filesystem::remove(file_path, ec);
    auto path = std::move(pair.first);
    auto stream = std::move(pair.second);
    stream->seekg(0, std::ios::end);

    if(stream->tellg() == 0) {
      Info::get().print("No methods found in current buffer");
      return methods;
    }
    stream->seekg(0, std::ios::beg);

    std::string line;
    bool all_kinds = this->language && (this->language->get_id() == "markdown" || this->language->get_id() == "json");
    while(std::getline(*stream, line)) {
      auto location = Ctags::get_location(line, true);
      if(all_kinds || location.kind == 'f' || location.kind == 's' || location.kind == 'm')
        methods.emplace_back(Offset(location.line, location.index), location.source);
    }
    std::sort(methods.begin(), methods.end(), [](const std::pair<Offset, std::string> &e1, const std::pair<Offset, std::string> &e2) {
      return e1.first < e2.first;
    });

    if(methods.empty())
      Info::get().print("No methods found in current buffer");
    return methods;
  };
}

Source::GenericView::~GenericView() {
  autocomplete.state = Autocomplete::State::idle;
  if(autocomplete.thread.joinable())
    autocomplete.thread.join();
}

void Source::GenericView::parse_language_file(bool &has_context_class, const boost::property_tree::ptree &pt) {
  bool case_insensitive = false;
  for(auto &node : pt) {
    if(node.first == "<xmlcomment>") {
      auto data = static_cast<std::string>(node.second.data());
      std::transform(data.begin(), data.end(), data.begin(), ::tolower);
      if(data.find("case insensitive") != std::string::npos)
        case_insensitive = true;
    }
    else if(node.first == "keyword") {
      auto data = static_cast<std::string>(node.second.data());
      keywords.emplace(data);

      if(case_insensitive) {
        std::transform(data.begin(), data.end(), data.begin(), ::tolower);
        keywords.emplace(data);
      }
    }
    else if(!has_context_class && node.first == "context") {
      auto class_attribute = node.second.get<std::string>("<xmlattr>.class", "");
      auto class_disabled_attribute = node.second.get<std::string>("<xmlattr>.class-disabled", "");
      if(class_attribute.size() > 0 || class_disabled_attribute.size() > 0)
        has_context_class = true;
    }
    try {
      parse_language_file(has_context_class, node.second);
    }
    catch(const std::exception &e) {
    }
  }
}

std::vector<std::pair<Gtk::TextIter, Gtk::TextIter>> Source::GenericView::get_words(const Gtk::TextIter &start, const Gtk::TextIter &end) {
  std::vector<std::pair<Gtk::TextIter, Gtk::TextIter>> words;

  auto iter = start;
  while(iter && iter < end) {
    if(is_token_char(*iter)) {
      auto word = get_token_iters(iter);
      if(!(*word.first >= '0' && *word.first <= '9') && (word.second.get_offset() - word.first.get_offset()) >= 3) // Minimum word length: 3
        words.emplace_back(word.first, word.second);
      iter = word.second;
    }
    iter.forward_char();
  }

  return words;
}

void Source::GenericView::setup_buffer_words() {
  {
    auto words = get_words(get_buffer()->begin(), get_buffer()->end());
    LockGuard lock(buffer_words_mutex);
    for(auto &word : words) {
      auto result = buffer_words.emplace(get_buffer()->get_text(word.first, word.second), 1);
      if(!result.second)
        ++(result.first->second);
    }
  }

  // Remove changed word at insert
  get_buffer()->signal_insert().connect([this](const Gtk::TextBuffer::iterator &iter_, const Glib::ustring &text, int bytes) {
    auto iter = iter_;
    if(!is_token_char(*iter))
      iter.backward_char();

    if(is_token_char(*iter)) {
      auto word = get_token_iters(iter);
      if(word.second.get_offset() - word.first.get_offset() >= 3) {
        LockGuard lock(buffer_words_mutex);
        auto it = buffer_words.find(get_buffer()->get_text(word.first, word.second));
        if(it != buffer_words.end()) {
          if(it->second > 1)
            --(it->second);
          else
            buffer_words.erase(it);
        }
      }
    }
  }, false);

  // Add all words between start and end of insert
  get_buffer()->signal_insert().connect([this](const Gtk::TextBuffer::iterator &iter, const Glib::ustring &text, int bytes) {
    auto start = iter;
    auto end = iter;
    start.backward_chars(text.size());
    if(!is_token_char(*start))
      start.backward_char();
    end.forward_char();

    auto words = get_words(start, end);
    LockGuard lock(buffer_words_mutex);
    for(auto &word : words) {
      auto result = buffer_words.emplace(get_buffer()->get_text(word.first, word.second), 1);
      if(!result.second)
        ++(result.first->second);
    }
  });

  // Remove words within text that was removed
  get_buffer()->signal_erase().connect([this](const Gtk::TextBuffer::iterator &start_, const Gtk::TextBuffer::iterator &end_) {
    auto start = start_;
    auto end = end_;
    if(!is_token_char(*start))
      start.backward_char();
    end.forward_char();
    auto words = get_words(start, end);
    LockGuard lock(buffer_words_mutex);
    for(auto &word : words) {
      auto it = buffer_words.find(get_buffer()->get_text(word.first, word.second));
      if(it != buffer_words.end()) {
        if(it->second > 1)
          --(it->second);
        else
          buffer_words.erase(it);
      }
    }
  }, false);

  // Add new word resulting from erased text
  get_buffer()->signal_erase().connect([this](const Gtk::TextBuffer::iterator &start_, const Gtk::TextBuffer::iterator & /*end*/) {
    auto start = start_;
    if(!is_token_char(*start))
      start.backward_char();
    if(is_token_char(*start)) {
      auto word = get_token_iters(start);
      if(word.second.get_offset() - word.first.get_offset() >= 3) {
        LockGuard lock(buffer_words_mutex);
        auto result = buffer_words.emplace(get_buffer()->get_text(word.first, word.second), 1);
        if(!result.second)
          ++(result.first->second);
      }
    }
  });
}

void Source::GenericView::setup_autocomplete() {
  non_interactive_completion = [this] {
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
      return;
    autocomplete.run();
  };

  autocomplete.reparse = [this] {
    autocomplete_comment.clear();
    autocomplete_insert.clear();
  };

  autocomplete.is_continue_key = [](guint keyval) {
    if((keyval >= '0' && keyval <= '9') || (keyval >= 'a' && keyval <= 'z') || (keyval >= 'A' && keyval <= 'Z') || keyval == '_' || gdk_keyval_to_unicode(keyval) >= 0x00C0)
      return true;

    return false;
  };

  autocomplete.is_restart_key = [](guint keyval) {
    return false;
  };

  autocomplete.run_check = [this]() {
    auto start = get_buffer()->get_insert()->get_iter();
    auto end = start;
    size_t count = 0;
    while(start.backward_char() && ((*start >= '0' && *start <= '9') || (*start >= 'a' && *start <= 'z') || (*start >= 'A' && *start <= 'Z') || *start == '_' || *start >= 0x00C0))
      ++count;
    if((start.is_start() || start.forward_char()) && count >= 3 && !(*start >= '0' && *start <= '9')) {
      LockGuard lock1(autocomplete.prefix_mutex);
      LockGuard lock2(buffer_words_mutex);
      autocomplete.prefix = get_buffer()->get_text(start, end);
      show_prefix_buffer_word = buffer_words.find(autocomplete.prefix) != buffer_words.end();
      return true;
    }
    else if(!interactive_completion) {
      auto end_iter = get_buffer()->get_insert()->get_iter();
      auto iter = end_iter;
      while(iter.backward_char() && autocomplete.is_continue_key(*iter)) {
      }
      if(iter != end_iter)
        iter.forward_char();
      LockGuard lock1(autocomplete.prefix_mutex);
      LockGuard lock2(buffer_words_mutex);
      autocomplete.prefix = get_buffer()->get_text(iter, end_iter);
      show_prefix_buffer_word = buffer_words.find(autocomplete.prefix) != buffer_words.end();
      return true;
    }

    return false;
  };

  autocomplete.before_add_rows = [this] {
    status_state = "autocomplete...";
    if(update_status_state)
      update_status_state(this);
  };

  autocomplete.after_add_rows = [this] {
    status_state = "";
    if(update_status_state)
      update_status_state(this);
  };

  autocomplete.on_add_rows_error = [this] {
    autocomplete_comment.clear();
    autocomplete_insert.clear();
  };

  autocomplete.add_rows = [this](std::string &buffer, int line_number, int column) {
    if(autocomplete.state == Autocomplete::State::starting) {
      autocomplete_comment.clear();
      autocomplete_insert.clear();

      std::string prefix;
      {
        LockGuard lock(autocomplete.prefix_mutex);
        prefix = autocomplete.prefix;
      }
      for(auto &keyword : keywords) {
        if(prefix.compare(0, prefix.size(), keyword, 0, prefix.size()) == 0) {
          autocomplete.rows.emplace_back(keyword);
          autocomplete_insert.emplace_back(keyword);
          autocomplete_comment.emplace_back("");
        }
      }
      {
        LockGuard lock(buffer_words_mutex);
        for(auto &buffer_word : buffer_words) {
          if((show_prefix_buffer_word || buffer_word.first.size() > prefix.size()) && prefix.compare(0, prefix.size(), buffer_word.first, 0, prefix.size()) == 0 &&
             keywords.find(buffer_word.first) == keywords.end()) {
            autocomplete.rows.emplace_back(buffer_word.first);
            autocomplete_insert.emplace_back(buffer_word.first);
            autocomplete_comment.emplace_back("");
          }
        }
      }
      LockGuard lock(snippets_mutex);
      if(snippets) {
        for(auto &snippet : *snippets) {
          if(prefix.compare(0, prefix.size(), snippet.prefix, 0, prefix.size()) == 0) {
            autocomplete.rows.emplace_back(snippet.prefix);
            autocomplete_insert.emplace_back(snippet.body);
            autocomplete_comment.emplace_back(snippet.description);
          }
        }
      }
    }
  };

  autocomplete.on_show = [this] {
    hide_tooltips();
  };

  autocomplete.on_hide = [this] {
    autocomplete_comment.clear();
    autocomplete_insert.clear();
  };

  autocomplete.on_select = [this](unsigned int index, const std::string &text, bool hide_window) {
    Glib::ustring insert = hide_window ? autocomplete_insert[index] : text;

    get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());

    if(hide_window)
      insert_snippet(CompletionDialog::get()->start_mark->get_iter(), insert);
    else
      get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert);
  };

  autocomplete.get_tooltip = [this](unsigned int index) {
    return autocomplete_comment[index];
  };
}
