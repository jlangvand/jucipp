#ifndef JUCI_SOURCE_CLANG_H_
#define JUCI_SOURCE_CLANG_H_

#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <boost/regex.hpp>
#include "clangmm.h"
#include "source.h"
#include "terminal.h"

namespace Source {
  class ClangViewParse : public View {
  protected:
    enum class ParseProcessState {IDLE, STARTING, PREPROCESSING, PROCESSING, POSTPROCESSING};
    enum class ParseState {WORKING, RESTARTING, STOP};
  public:
    class TokenRange {
    public:
      TokenRange(std::pair<clang::Offset, clang::Offset> offsets, int kind):
        offsets(offsets), kind(kind) {}
      std::pair<clang::Offset, clang::Offset> offsets;
      int kind;
    };
    
    ClangViewParse(const boost::filesystem::path &file_path, const boost::filesystem::path& project_path, Glib::RefPtr<Gsv::Language> language);
    bool on_key_press_event(GdkEventKey* key) override;
    
    void configure() override;
    
    void soft_reparse() override;
  protected:
    void parse_initialize();
    std::unique_ptr<clang::TranslationUnit> clang_tu;
    std::unique_ptr<clang::Tokens> clang_tokens;
    sigc::connection delayed_reparse_connection;
    
    std::shared_ptr<Terminal::InProgress> parsing_in_progress;
    
    void show_diagnostic_tooltips(const Gdk::Rectangle &rectangle) override;
    void show_type_tooltips(const Gdk::Rectangle &rectangle) override;
    
    boost::regex bracket_regex;
    boost::regex no_bracket_statement_regex;
    boost::regex no_bracket_no_para_statement_regex;
    
    std::set<int> diagnostic_offsets;
    std::vector<FixIt> fix_its;
    
    std::thread parse_thread;
    std::mutex parse_mutex;
    std::atomic<ParseProcessState> parse_process_state;
    std::atomic<ParseState> parse_state;
    sigc::connection parse_done_connection;
    sigc::connection parse_start_connection;
    sigc::connection parse_fail_connection;
  private:
    Glib::Dispatcher parse_preprocess;
    Glib::Dispatcher parse_postprocess;
    Glib::Dispatcher parse_error;
    Glib::ustring parse_thread_buffer;
    
    void update_syntax();
    std::set<std::string> last_syntax_tags;
    void update_diagnostics();
    
    static clang::Index clang_index;
    std::vector<std::string> get_compilation_commands();
  };
    
  class ClangViewAutocomplete : public ClangViewParse {
  public:
    class AutoCompleteData {
    public:
      explicit AutoCompleteData(const std::vector<clang::CompletionChunk> &chunks) :
        chunks(chunks) { }
      std::vector<clang::CompletionChunk> chunks;
      std::string brief_comments;
    };
    
    ClangViewAutocomplete(const boost::filesystem::path &file_path, const boost::filesystem::path& project_path, Glib::RefPtr<Gsv::Language> language);
    bool on_key_press_event(GdkEventKey* key) override;
    
    virtual void async_delete();
    bool full_reparse() override;
  protected:
    std::thread autocomplete_thread;
    sigc::connection autocomplete_done_connection;
    sigc::connection autocomplete_fail_connection;
    sigc::connection do_delete_object_connection;
    sigc::connection do_restart_parse_connection;
  private:
    void start_autocomplete();
    void autocomplete();
    std::unique_ptr<CompletionDialog> completion_dialog;
    bool completion_dialog_shown=false;
    std::vector<AutoCompleteData> get_autocomplete_suggestions(const std::string &buffer, int line_number, int column);
    Glib::Dispatcher autocomplete_done;
    Glib::Dispatcher autocomplete_fail;
    bool autocomplete_starting=false;
    std::atomic<bool> autocomplete_cancel_starting;
    guint last_keyval=0;
    std::string prefix;
    std::mutex prefix_mutex;
    
    Glib::Dispatcher do_delete_object;
    Glib::Dispatcher do_full_reparse;
    std::thread delete_thread;
    std::thread full_reparse_thread;
    bool full_reparse_running=false;
  };

  class ClangViewRefactor : public ClangViewAutocomplete {
  public:
    ClangViewRefactor(const boost::filesystem::path &file_path, const boost::filesystem::path& project_path, Glib::RefPtr<Gsv::Language> language);
  protected:
    sigc::connection delayed_tag_similar_tokens_connection;
  private:
    std::list<std::pair<Glib::RefPtr<Gtk::TextMark>, Glib::RefPtr<Gtk::TextMark> > > similar_token_marks;
    void tag_similar_tokens(const Token &token);
    Glib::RefPtr<Gtk::TextTag> similar_tokens_tag;
    Token last_tagged_token;
    bool renaming=false;
  };
  
  class ClangView : public ClangViewRefactor {
  public:
    ClangView(const boost::filesystem::path &file_path, const boost::filesystem::path& project_path, Glib::RefPtr<Gsv::Language> language);
    void async_delete() override;
  };
}

#endif  // JUCI_SOURCE_CLANG_H_
