#pragma once
#include "autocomplete.h"
#include "mutex.h"
#include "process.hpp"
#include "source.h"
#include <atomic>
#include <boost/property_tree/json_parser.hpp>
#include <list>
#include <map>
#include <set>
#include <sstream>

namespace Source {
  class LanguageProtocolView;
}

namespace LanguageProtocol {
  class Offset {
  public:
    Offset(const boost::property_tree::ptree &pt);
    int line, character;

    bool operator<(const Offset &rhs) const {
      return line < rhs.line || (line == rhs.line && character < rhs.character);
    }
    bool operator==(const Offset &rhs) const {
      return line == rhs.line && character == rhs.character;
    }
  };

  class Range {
  public:
    Range(const boost::property_tree::ptree &pt);
    Offset start, end;

    bool operator<(const Range &rhs) const {
      return start < rhs.start || (start == rhs.start && end < rhs.end);
    }
    bool operator==(const Range &rhs) const {
      return start == rhs.start && end == rhs.end;
    }
  };

  class Location {
  public:
    Location(const boost::property_tree::ptree &pt, std::string file_ = {});
    std::string file;
    Range range;

    bool operator<(const Location &rhs) const {
      return file < rhs.file || (file == rhs.file && range < rhs.range);
    }
    bool operator==(const Location &rhs) const {
      return file == rhs.file && range == rhs.range;
    }
  };

  class Diagnostic {
  public:
    class RelatedInformation {
    public:
      RelatedInformation(const boost::property_tree::ptree &pt);
      std::string message;
      Location location;
    };

    Diagnostic(const boost::property_tree::ptree &pt);
    std::string message;
    Range range;
    int severity;
    std::vector<RelatedInformation> related_informations;
  };

  class TextEdit {
  public:
    TextEdit(const boost::property_tree::ptree &pt, std::string new_text_ = {});
    Range range;
    std::string new_text;
  };

  class Capabilities {
  public:
    enum class TextDocumentSync { none = 0,
                                  full,
                                  incremental };
    TextDocumentSync text_document_sync = TextDocumentSync::none;
    bool hover = false;
    bool completion = false;
    bool signature_help = false;
    bool definition = false;
    bool references = false;
    bool document_highlight = false;
    bool workspace_symbol = false;
    bool document_symbol = false;
    bool document_formatting = false;
    bool document_range_formatting = false;
    bool rename = false;
    bool type_coverage = false;
  };

  class Client {
    Client(boost::filesystem::path root_path, std::string language_id);
    boost::filesystem::path root_path;
    std::string language_id;

    Capabilities capabilities;

    Mutex views_mutex;
    std::set<Source::LanguageProtocolView *> views GUARDED_BY(views_mutex);

    Mutex initialize_mutex;
    bool initialized GUARDED_BY(initialize_mutex) = false;

    Mutex read_write_mutex;
    std::unique_ptr<TinyProcessLib::Process> process GUARDED_BY(read_write_mutex);

    std::stringstream server_message_stream;
    size_t server_message_size = static_cast<size_t>(-1);
    size_t server_message_content_pos;
    bool header_read = false;

    size_t message_id GUARDED_BY(read_write_mutex) = 1;

    std::map<size_t, std::pair<Source::LanguageProtocolView *, std::function<void(const boost::property_tree::ptree &, bool error)>>> handlers GUARDED_BY(read_write_mutex);

    Mutex timeout_threads_mutex;
    std::vector<std::thread> timeout_threads GUARDED_BY(timeout_threads_mutex);

  public:
    static std::shared_ptr<Client> get(const boost::filesystem::path &file_path, const std::string &language_id);

    ~Client();

    Capabilities initialize(Source::LanguageProtocolView *view);
    void close(Source::LanguageProtocolView *view);

    void parse_server_message();
    void write_request(Source::LanguageProtocolView *view, const std::string &method, const std::string &params, std::function<void(const boost::property_tree::ptree &, bool)> &&function = nullptr);
    void write_notification(const std::string &method, const std::string &params);
    void handle_server_request(const std::string &method, const boost::property_tree::ptree &params);
  };
} // namespace LanguageProtocol

namespace Source {
  class LanguageProtocolView : public View {
  public:
    LanguageProtocolView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language, std::string language_id_);
    void initialize();
    void close();
    ~LanguageProtocolView() override;

    void rename(const boost::filesystem::path &path) override;
    bool save() override;

    void update_diagnostics(std::vector<LanguageProtocol::Diagnostic> &&diagnostics);

  private:
    void update_diagnostics(const std::vector<LanguageProtocol::Diagnostic> &diagnostics);

  public:
    Gtk::TextIter get_iter_at_line_pos(int line, int pos) override;

    std::string uri;

  protected:
    void show_type_tooltips(const Gdk::Rectangle &rectangle) override;
    void apply_similar_symbol_tag() override;
    void apply_clickable_tag(const Gtk::TextIter &iter) override;

  private:
    bool initialized = false;

    std::string language_id;
    LanguageProtocol::Capabilities capabilities;

    std::shared_ptr<LanguageProtocol::Client> client;

    size_t document_version = 1;

    std::thread initialize_thread;
    Dispatcher dispatcher;

    void setup_navigation_and_refactoring();

    void escape_text(std::string &text);

    void tag_similar_symbols();

    Offset get_declaration(const Gtk::TextIter &iter);

    std::unique_ptr<Autocomplete> autocomplete;
    void setup_signals();
    void setup_autocomplete();
    std::vector<std::string> autocomplete_comment;
    std::vector<std::string> autocomplete_insert;
    std::atomic<bool> autocomplete_enable_snippets = {false};
    bool autocomplete_show_arguments = false;
    sigc::connection autocomplete_delayed_show_arguments_connection;

    bool has_named_parameters();

    std::vector<LanguageProtocol::Diagnostic> last_diagnostics;

    sigc::connection update_type_coverage_connection;
    std::vector<std::pair<Glib::RefPtr<Gtk::TextMark>, Glib::RefPtr<Gtk::TextMark>>> type_coverage_marks;
    size_t num_warnings = 0, num_errors = 0, num_fix_its = 0;
    void update_type_coverage();
    std::atomic<int> update_type_coverage_retries = {60};
  };
} // namespace Source
