#include "source_language_protocol.h"
#include "filesystem.h"
#include "info.h"
#include "notebook.h"
#include "project.h"
#include "selection_dialog.h"
#include "terminal.h"
#ifdef JUCI_ENABLE_DEBUG
#include "debug_lldb.h"
#endif
#include "config.h"
#include "menu.h"
#include <future>
#include <limits>
#include <regex>
#include <unordered_map>

const std::string type_coverage_message = "Un-type checked code. Consider adding type annotations.";

LanguageProtocol::Offset::Offset(const boost::property_tree::ptree &pt) : line(pt.get<int>("line")), character(pt.get<int>("character")) {}

LanguageProtocol::Range::Range(const boost::property_tree::ptree &pt) : start(pt.get_child("start")), end(pt.get_child("end")) {}

LanguageProtocol::Location::Location(const boost::property_tree::ptree &pt, std::string file_) : range(pt.get_child("range")) {
  if(file_.empty()) {
    file = filesystem::get_path_from_uri(pt.get<std::string>("uri")).string();
  }
  else
    file = std::move(file_);
}

LanguageProtocol::Diagnostic::RelatedInformation::RelatedInformation(const boost::property_tree::ptree &pt) : message(pt.get<std::string>("message")), location(pt.get_child("location")) {}

LanguageProtocol::Diagnostic::Diagnostic(const boost::property_tree::ptree &pt) : message(pt.get<std::string>("message")), range(pt.get_child("range")), severity(pt.get<int>("severity", 0)) {
  auto related_information_it = pt.get_child("relatedInformation", boost::property_tree::ptree());
  for(auto it = related_information_it.begin(); it != related_information_it.end(); ++it)
    related_informations.emplace_back(it->second);
}

LanguageProtocol::TextEdit::TextEdit(const boost::property_tree::ptree &pt, std::string new_text_) : range(pt.get_child("range")), new_text(new_text_.empty() ? pt.get<std::string>("newText") : std::move(new_text_)) {}

LanguageProtocol::Client::Client(boost::filesystem::path root_path_, std::string language_id_) : root_path(std::move(root_path_)), language_id(std::move(language_id_)) {
  process = std::make_unique<TinyProcessLib::Process>(filesystem::escape_argument(language_id + "-language-server"), root_path.string(), [this](const char *bytes, size_t n) {
    server_message_stream.write(bytes, n);
    parse_server_message();
  }, [](const char *bytes, size_t n) {
    std::cerr.write(bytes, n);
  }, true, TinyProcessLib::Config{1048576});
}

std::shared_ptr<LanguageProtocol::Client> LanguageProtocol::Client::get(const boost::filesystem::path &file_path, const std::string &language_id) {
  boost::filesystem::path root_path;
  auto build = Project::Build::create(file_path);
  if(!build->project_path.empty())
    root_path = build->project_path;
  else
    root_path = file_path.parent_path();

  auto cache_id = root_path.string() + '|' + language_id;

  static std::unordered_map<std::string, std::weak_ptr<Client>> cache;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(cache_id);
  if(it == cache.end())
    it = cache.emplace(cache_id, std::weak_ptr<Client>()).first;
  auto instance = it->second.lock();
  if(!instance)
    it->second = instance = std::shared_ptr<Client>(new Client(root_path, language_id), [](Client *client_ptr) {
      std::thread delete_thread([client_ptr] {
        delete client_ptr;
      });
      delete_thread.detach();
    });
  return instance;
}

LanguageProtocol::Client::~Client() {
  std::promise<void> result_processed;
  write_request(nullptr, "shutdown", "", [this, &result_processed](const boost::property_tree::ptree &result, bool error) {
    if(!error)
      this->write_notification("exit", "");
    result_processed.set_value();
  });
  result_processed.get_future().get();

  std::lock_guard<std::mutex> lock(timeout_threads_mutex);
  for(auto &thread : timeout_threads)
    thread.join();

  int exit_status = -1;
  for(size_t c = 0; c < 20; ++c) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if(process->try_get_exit_status(exit_status))
      break;
  }
  if(Config::get().log.language_server)
    std::cout << "Language server exit status: " << exit_status << std::endl;
  if(exit_status == -1)
    process->kill();
}

LanguageProtocol::Capabilities LanguageProtocol::Client::initialize(Source::LanguageProtocolView *view) {
  if(view) {
    std::lock_guard<std::mutex> lock(views_mutex);
    views.emplace(view);
  }

  std::lock_guard<std::mutex> lock(initialize_mutex);

  if(initialized)
    return capabilities;

  std::promise<void> result_processed;
  write_request(nullptr, "initialize", "\"processId\":" + std::to_string(process->get_id()) + R"(,"rootUri":")" + filesystem::get_uri_from_path(root_path) + R"(","capabilities":{"workspace":{"didChangeConfiguration":{"dynamicRegistration":true},"didChangeWatchedFiles":{"dynamicRegistration":true},"symbol":{"dynamicRegistration":true},"executeCommand":{"dynamicRegistration":true}},"textDocument":{"synchronization":{"dynamicRegistration":true,"willSave":true,"willSaveWaitUntil":true,"didSave":true},"completion":{"dynamicRegistration":true,"completionItem":{"snippetSupport":true}},"hover":{"dynamicRegistration":true},"signatureHelp":{"dynamicRegistration":true},"definition":{"dynamicRegistration":true},"references":{"dynamicRegistration":true},"documentHighlight":{"dynamicRegistration":true},"documentSymbol":{"dynamicRegistration":true},"codeAction":{"dynamicRegistration":true},"codeLens":{"dynamicRegistration":true},"formatting":{"dynamicRegistration":true},"rangeFormatting":{"dynamicRegistration":true},"onTypeFormatting":{"dynamicRegistration":true},"rename":{"dynamicRegistration":true},"documentLink":{"dynamicRegistration":true}}},"initializationOptions":{"omitInitBuild":true},"trace":"off")", [this, &result_processed](const boost::property_tree::ptree &result, bool error) {
    if(!error) {
      auto capabilities_pt = result.find("capabilities");
      if(capabilities_pt != result.not_found()) {
        try {
          capabilities.text_document_sync = static_cast<LanguageProtocol::Capabilities::TextDocumentSync>(capabilities_pt->second.get<int>("textDocumentSync"));
        }
        catch(...) {
          capabilities.text_document_sync = static_cast<LanguageProtocol::Capabilities::TextDocumentSync>(capabilities_pt->second.get<int>("textDocumentSync.change", 0));
        }
        capabilities.hover = capabilities_pt->second.get<bool>("hoverProvider", false);
        capabilities.completion = capabilities_pt->second.find("completionProvider") != capabilities_pt->second.not_found() ? true : false;
        capabilities.signature_help = capabilities_pt->second.find("signatureHelpProvider") != capabilities_pt->second.not_found() ? true : false;
        capabilities.definition = capabilities_pt->second.get<bool>("definitionProvider", false);
        capabilities.references = capabilities_pt->second.get<bool>("referencesProvider", false);
        capabilities.document_highlight = capabilities_pt->second.get<bool>("documentHighlightProvider", false);
        capabilities.workspace_symbol = capabilities_pt->second.get<bool>("workspaceSymbolProvider", false);
        capabilities.document_symbol = capabilities_pt->second.get<bool>("documentSymbolProvider", false);
        capabilities.document_formatting = capabilities_pt->second.get<bool>("documentFormattingProvider", false);
        capabilities.document_range_formatting = capabilities_pt->second.get<bool>("documentRangeFormattingProvider", false);
        capabilities.rename = capabilities_pt->second.get<bool>("renameProvider", false);
        capabilities.type_coverage = capabilities_pt->second.get<bool>("typeCoverageProvider", false);
      }

      write_notification("initialized", "");
      if(language_id == "rust")
        write_notification("workspace/didChangeConfiguration", R"("settings":{"rust":{"sysroot":null,"target":null,"rustflags":null,"clear_env_rust_log":true,"build_lib":null,"build_bin":null,"cfg_test":false,"unstable_features":false,"wait_to_build":500,"show_warnings":true,"goto_def_racer_fallback":false,"use_crate_blacklist":true,"build_on_save":false,"workspace_mode":true,"analyze_package":null,"features":[],"all_features":false,"no_default_features":false}})");
    }
    result_processed.set_value();
  });
  result_processed.get_future().get();

  initialized = true;
  return capabilities;
}

void LanguageProtocol::Client::close(Source::LanguageProtocolView *view) {
  {
    std::lock_guard<std::mutex> lock(views_mutex);
    auto it = views.find(view);
    if(it != views.end())
      views.erase(it);
  }
  std::lock_guard<std::mutex> lock(read_write_mutex);
  for(auto it = handlers.begin(); it != handlers.end();) {
    if(it->second.first == view)
      it = handlers.erase(it);
    else
      it++;
  }
}

void LanguageProtocol::Client::parse_server_message() {
  if(!header_read) {
    server_message_size = static_cast<size_t>(-1);
    server_message_stream.seekg(0, std::ios::beg);

    std::string line;
    while(!header_read && std::getline(server_message_stream, line)) {
      if(!line.empty()) {
        if(line.back() == '\r')
          line.pop_back();
        if(line.compare(0, 16, "Content-Length: ") == 0) {
          try {
            server_message_size = static_cast<size_t>(std::stoul(line.substr(16)));
          }
          catch(...) {
          }
        }
      }
      if(line.empty() && server_message_size != static_cast<size_t>(-1)) {
        server_message_content_pos = server_message_stream.tellg();
        server_message_size += server_message_content_pos;
        header_read = true;
      }
    }
  }

  if(header_read) {
    server_message_stream.seekg(0, std::ios::end);
    size_t read_size = server_message_stream.tellg();
    std::stringstream tmp;
    if(read_size >= server_message_size) {
      if(read_size > server_message_size) {
        server_message_stream.seekg(server_message_size, std::ios::beg);
        server_message_stream.seekp(server_message_size, std::ios::beg);
        for(size_t c = server_message_size; c < read_size; ++c) {
          tmp.put(server_message_stream.get());
          server_message_stream.put(' ');
        }
      }

      server_message_stream.seekg(server_message_content_pos, std::ios::beg);
      boost::property_tree::ptree pt;
      boost::property_tree::read_json(server_message_stream, pt);

      if(Config::get().log.language_server) {
        std::cout << "language server: ";
        boost::property_tree::write_json(std::cout, pt);
      }

      auto message_id = pt.get<size_t>("id", 0);
      auto result_it = pt.find("result");
      auto error_it = pt.find("error");
      {
        std::unique_lock<std::mutex> lock(read_write_mutex);
        if(result_it != pt.not_found()) {
          if(message_id) {
            auto id_it = handlers.find(message_id);
            if(id_it != handlers.end()) {
              auto function = std::move(id_it->second.second);
              handlers.erase(id_it->first);
              lock.unlock();
              function(result_it->second, false);
              lock.lock();
            }
          }
        }
        else if(error_it != pt.not_found()) {
          if(!Config::get().log.language_server)
            boost::property_tree::write_json(std::cerr, pt);
          if(message_id) {
            auto id_it = handlers.find(message_id);
            if(id_it != handlers.end()) {
              auto function = std::move(id_it->second.second);
              handlers.erase(id_it->first);
              lock.unlock();
              function(result_it->second, true);
              lock.lock();
            }
          }
        }
        else {
          auto method_it = pt.find("method");
          if(method_it != pt.not_found()) {
            auto params_it = pt.find("params");
            if(params_it != pt.not_found()) {
              lock.unlock();
              handle_server_request(method_it->second.get_value<std::string>(""), params_it->second);
              lock.lock();
            }
          }
        }
      }

      server_message_stream = std::stringstream();
      header_read = false;
      server_message_size = static_cast<size_t>(-1);

      tmp.seekg(0, std::ios::end);
      if(tmp.tellg() > 0) {
        tmp.seekg(0, std::ios::beg);
        server_message_stream << tmp.rdbuf();
        parse_server_message();
      }
    }
  }
}

void LanguageProtocol::Client::write_request(Source::LanguageProtocolView *view, const std::string &method, const std::string &params, std::function<void(const boost::property_tree::ptree &, bool error)> &&function) {
  std::unique_lock<std::mutex> lock(read_write_mutex);
  if(function) {
    handlers.emplace(message_id, std::make_pair(view, std::move(function)));

    auto message_id = this->message_id;
    std::lock_guard<std::mutex> lock(timeout_threads_mutex);
    timeout_threads.emplace_back([this, message_id] {
      for(size_t c = 0; c < 20; ++c) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::lock_guard<std::mutex> lock(read_write_mutex);
        auto id_it = handlers.find(message_id);
        if(id_it == handlers.end())
          return;
      }
      std::unique_lock<std::mutex> lock(read_write_mutex);
      auto id_it = handlers.find(message_id);
      if(id_it != handlers.end()) {
        Terminal::get().async_print("Request to language server timed out. If you suspect the server has crashed, please close and reopen all project source files.\n", true);
        auto function = std::move(id_it->second.second);
        handlers.erase(id_it->first);
        lock.unlock();
        function(boost::property_tree::ptree(), true);
        lock.lock();
      }
    });
  }
  std::string content(R"({"jsonrpc":"2.0","id":)" + std::to_string(message_id++) + R"(,"method":")" + method + R"(","params":{)" + params + "}}");
  auto message = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n" + content;
  if(Config::get().log.language_server)
    std::cout << "Language client: " << content << std::endl;
  if(!process->write(message)) {
    Terminal::get().async_print("Error writing to language protocol server. Please close and reopen all project source files.\n", true);
    auto id_it = handlers.find(message_id - 1);
    if(id_it != handlers.end()) {
      auto function = std::move(id_it->second.second);
      handlers.erase(id_it->first);
      lock.unlock();
      function(boost::property_tree::ptree(), true);
      lock.lock();
    }
  }
}

void LanguageProtocol::Client::write_notification(const std::string &method, const std::string &params) {
  std::lock_guard<std::mutex> lock(read_write_mutex);
  std::string content(R"({"jsonrpc":"2.0","method":")" + method + R"(","params":{)" + params + "}}");
  auto message = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n" + content;
  if(Config::get().log.language_server)
    std::cout << "Language client: " << content << std::endl;
  process->write(message);
}

void LanguageProtocol::Client::handle_server_request(const std::string &method, const boost::property_tree::ptree &params) {
  if(method == "textDocument/publishDiagnostics") {
    std::vector<Diagnostic> diagnostics;
    auto file = filesystem::get_path_from_uri(params.get<std::string>("uri", ""));
    if(!file.empty()) {
      auto diagnostics_pt = params.get_child("diagnostics", boost::property_tree::ptree());
      for(auto it = diagnostics_pt.begin(); it != diagnostics_pt.end(); ++it) {
        try {
          diagnostics.emplace_back(it->second);
        }
        catch(...) {
        }
      }
      std::lock_guard<std::mutex> lock(views_mutex);
      for(auto view : views) {
        if(file == view->file_path) {
          view->update_diagnostics(std::move(diagnostics));
          break;
        }
      }
    }
  }
}

Source::LanguageProtocolView::LanguageProtocolView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language, std::string language_id_)
    : Source::BaseView(file_path, language), Source::View(file_path, language), uri(filesystem::get_uri_from_path(file_path)), language_id(std::move(language_id_)), client(LanguageProtocol::Client::get(file_path, language_id)), autocomplete(this, interactive_completion, last_keyval, false) {
  configure();
  get_source_buffer()->set_language(language);
  get_source_buffer()->set_highlight_syntax(true);

  initialize();

  get_buffer()->signal_insert().connect([this](const Gtk::TextBuffer::iterator &start, const Glib::ustring &text_, int bytes) {
    std::string content_changes;
    if(capabilities.text_document_sync == LanguageProtocol::Capabilities::TextDocumentSync::NONE)
      return;
    if(capabilities.text_document_sync == LanguageProtocol::Capabilities::TextDocumentSync::INCREMENTAL) {
      std::string text = text_;
      escape_text(text);
      content_changes = R"({"range":{"start":{"line": )" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(}},"text":")" + text + "\"}";
    }
    else {
      std::string text = get_buffer()->get_text();
      escape_text(text);
      content_changes = R"({"text":")" + text + "\"}";
    }
    client->write_notification("textDocument/didChange", R"("textDocument":{"uri":")" + this->uri + R"(","version":)" + std::to_string(document_version++) + "},\"contentChanges\":[" + content_changes + "]");
  }, false);

  get_buffer()->signal_erase().connect([this](const Gtk::TextBuffer::iterator &start, const Gtk::TextBuffer::iterator &end) {
    std::string content_changes;
    if(capabilities.text_document_sync == LanguageProtocol::Capabilities::TextDocumentSync::NONE)
      return;
    if(capabilities.text_document_sync == LanguageProtocol::Capabilities::TextDocumentSync::INCREMENTAL)
      content_changes = R"({"range":{"start":{"line": )" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(end.get_line()) + ",\"character\":" + std::to_string(end.get_line_offset()) + R"(}},"text":""})";
    else {
      std::string text = get_buffer()->get_text();
      escape_text(text);
      content_changes = R"({"text":")" + text + "\"}";
    }
    client->write_notification("textDocument/didChange", R"("textDocument":{"uri":")" + this->uri + R"(","version":)" + std::to_string(document_version++) + "},\"contentChanges\":[" + content_changes + "]");
  }, false);
}

void Source::LanguageProtocolView::initialize() {
  status_diagnostics = std::make_tuple(0, 0, 0);
  if(update_status_diagnostics)
    update_status_diagnostics(this);

  status_state = "initializing...";
  if(update_status_state)
    update_status_state(this);

  set_editable(false);
  initialize_thread = std::thread([this] {
    auto capabilities = client->initialize(this);

    dispatcher.post([this, capabilities] {
      this->capabilities = capabilities;
      set_editable(true);

      std::string text = get_buffer()->get_text();
      escape_text(text);
      client->write_notification("textDocument/didOpen", R"("textDocument":{"uri":")" + uri + R"(","languageId":")" + language_id + R"(","version":)" + std::to_string(document_version++) + R"(,"text":")" + text + "\"}");

      if(!initialized) {
        setup_autocomplete();
        setup_navigation_and_refactoring();
        Menu::get().toggle_menu_items();
      }

      if(status_state == "initializing...") {
        status_state = "";
        if(update_status_state)
          update_status_state(this);
      }

      update_type_coverage();

      initialized = true;
    });
  });
}

void Source::LanguageProtocolView::close() {
  autocomplete_delayed_show_arguments_connection.disconnect();
  update_type_coverage_connection.disconnect();

  if(initialize_thread.joinable())
    initialize_thread.join();

  autocomplete.state = Autocomplete::State::IDLE;
  if(autocomplete.thread.joinable())
    autocomplete.thread.join();

  client->write_notification("textDocument/didClose", R"("textDocument":{"uri":")" + uri + "\"}");
  client->close(this);
  client = nullptr;
}

Source::LanguageProtocolView::~LanguageProtocolView() {
  close();
}

void Source::LanguageProtocolView::rename(const boost::filesystem::path &path) {
  // Reset view
  close();
  dispatcher.reset();
  Source::DiffView::rename(path);
  uri = filesystem::get_uri_from_path(path);
  client = LanguageProtocol::Client::get(file_path, language_id);
  initialize();
}

bool Source::LanguageProtocolView::save() {
  if(!Source::View::save())
    return false;

  update_type_coverage();

  return true;
}

void Source::LanguageProtocolView::setup_navigation_and_refactoring() {
  if(capabilities.document_formatting) {
    format_style = [this](bool continue_without_style_file) {
      if(!continue_without_style_file) {
        bool has_style_file = false;
        auto style_file_search_path = file_path.parent_path();
        auto style_file = '.' + language_id + "-format";

        while(true) {
          if(boost::filesystem::exists(style_file_search_path / style_file)) {
            has_style_file = true;
            break;
          }
          if(style_file_search_path == style_file_search_path.root_directory())
            break;
          style_file_search_path = style_file_search_path.parent_path();
        }

        if(!has_style_file && !continue_without_style_file)
          return;
      }

      std::vector<LanguageProtocol::TextEdit> text_edits;
      std::promise<void> result_processed;

      std::string method;
      std::string params;
      std::string options("\"tabSize\":" + std::to_string(tab_size) + ",\"insertSpaces\":" + (tab_char == ' ' ? "true" : "false"));
      if(get_buffer()->get_has_selection() && capabilities.document_range_formatting) {
        method = "textDocument/rangeFormatting";
        Gtk::TextIter start, end;
        get_buffer()->get_selection_bounds(start, end);
        params = R"("textDocument":{"uri":")" + uri + R"("},"range":{"start":{"line":)" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(end.get_line()) + ",\"character\":" + std::to_string(end.get_line_offset()) + "}},\"options\":{" + options + "}";
      }
      else {
        method = "textDocument/formatting";
        params = R"("textDocument":{"uri":")" + uri + R"("},"options":{)" + options + "}";
      }

      client->write_request(this, method, params, [&text_edits, &result_processed](const boost::property_tree::ptree &result, bool error) {
        if(!error) {
          for(auto it = result.begin(); it != result.end(); ++it) {
            try {
              text_edits.emplace_back(it->second);
            }
            catch(...) {
            }
          }
        }
        result_processed.set_value();
      });
      result_processed.get_future().get();

      auto end_iter = get_buffer()->end();
      // If entire buffer is replaced:
      if(text_edits.size() == 1 &&
         text_edits[0].range.start.line == 0 && text_edits[0].range.start.character == 0 &&
         (text_edits[0].range.end.line > end_iter.get_line() ||
          (text_edits[0].range.end.line == end_iter.get_line() && text_edits[0].range.end.character >= end_iter.get_line_offset()))) {
        replace_text(text_edits[0].new_text);
      }
      else {
        get_buffer()->begin_user_action();
        for(auto it = text_edits.rbegin(); it != text_edits.rend(); ++it) {
          auto start = get_iter_at_line_pos(it->range.start.line, it->range.start.character);
          auto end = get_iter_at_line_pos(it->range.end.line, it->range.end.character);
          get_buffer()->erase(start, end);
          start = get_iter_at_line_pos(it->range.start.line, it->range.start.character);
          get_buffer()->insert(start, it->new_text);
        }
        get_buffer()->end_user_action();
      }
    };
  }

  if(capabilities.definition) {
    get_declaration_location = [this]() {
      auto offset = get_declaration(get_buffer()->get_insert()->get_iter());
      if(!offset)
        Info::get().print("No declaration found");
      return offset;
    };
    get_declaration_or_implementation_locations = [this]() {
      std::vector<Offset> offsets;
      auto offset = get_declaration_location();
      if(offset)
        offsets.emplace_back(std::move(offset));
      return offsets;
    };
  }

  if(capabilities.references || capabilities.document_highlight) {
    get_usages = [this] {
      auto iter = get_buffer()->get_insert()->get_iter();
      std::set<LanguageProtocol::Location> locations;
      std::promise<void> result_processed;

      std::string method;
      if(capabilities.references)
        method = "textDocument/references";
      else
        method = "textDocument/documentHighlight";

      client->write_request(this, method, R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "context": {"includeDeclaration": true})", [this, &locations, &result_processed](const boost::property_tree::ptree &result, bool error) {
        if(!error) {
          try {
            for(auto it = result.begin(); it != result.end(); ++it)
              locations.emplace(it->second, !capabilities.references ? file_path.string() : std::string());
          }
          catch(...) {
            locations.clear();
          }
        }
        result_processed.set_value();
      });
      result_processed.get_future().get();

      auto embolden_token = [](std::string &line_, int token_start_pos, int token_end_pos) {
        Glib::ustring line = line_;
        if(static_cast<size_t>(token_start_pos) > line.size() || static_cast<size_t>(token_end_pos) > line.size())
          return;

        //markup token as bold
        size_t pos = 0;
        while((pos = line.find('&', pos)) != Glib::ustring::npos) {
          size_t pos2 = line.find(';', pos + 2);
          if(static_cast<size_t>(token_start_pos) > pos) {
            token_start_pos += pos2 - pos;
            token_end_pos += pos2 - pos;
          }
          else if(static_cast<size_t>(token_end_pos) > pos)
            token_end_pos += pos2 - pos;
          else
            break;
          pos = pos2 + 1;
        }
        line.insert(token_end_pos, "</b>");
        line.insert(token_start_pos, "<b>");

        size_t start_pos = 0;
        while(start_pos < line.size() && (line[start_pos] == ' ' || line[start_pos] == '\t'))
          ++start_pos;
        if(start_pos > 0)
          line.erase(0, start_pos);

        line_ = line.raw();
      };

      std::unordered_map<std::string, std::vector<std::string>> file_lines;
      std::vector<std::pair<Offset, std::string>> usages;
      auto c = static_cast<size_t>(-1);
      for(auto &location : locations) {
        ++c;
        usages.emplace_back(Offset(location.range.start.line, location.range.start.character, location.file), std::string());
        auto &usage = usages.back();
        auto view_it = views.end();
        for(auto it = views.begin(); it != views.end(); ++it) {
          if(location.file == (*it)->file_path) {
            view_it = it;
            break;
          }
        }
        if(view_it != views.end()) {
          if(location.range.start.line < (*view_it)->get_buffer()->get_line_count()) {
            auto start = (*view_it)->get_buffer()->get_iter_at_line(location.range.start.line);
            auto end = start;
            end.forward_to_line_end();
            usage.second = Glib::Markup::escape_text((*view_it)->get_buffer()->get_text(start, end));
            embolden_token(usage.second, location.range.start.character, location.range.end.character);
          }
        }
        else {
          auto it = file_lines.find(location.file);
          if(it == file_lines.end()) {
            std::ifstream ifs(location.file);
            if(ifs) {
              std::vector<std::string> lines;
              std::string line;
              while(std::getline(ifs, line)) {
                if(!line.empty() && line.back() == '\r')
                  line.pop_back();
                lines.emplace_back(line);
              }
              auto pair = file_lines.emplace(location.file, lines);
              it = pair.first;
            }
            else {
              auto pair = file_lines.emplace(location.file, std::vector<std::string>());
              it = pair.first;
            }
          }

          if(static_cast<size_t>(location.range.start.line) < it->second.size()) {
            usage.second = Glib::Markup::escape_text(it->second[location.range.start.line]);
            embolden_token(usage.second, location.range.start.character, location.range.end.character);
          }
        }
      }

      if(locations.empty())
        Info::get().print("No symbol found at current cursor location");

      return usages;
    };
  }

  get_token_spelling = [this]() -> std::string {
    auto start = get_buffer()->get_insert()->get_iter();
    auto end = start;
    auto previous = start;
    while(previous.backward_char() && ((*previous >= 'A' && *previous <= 'Z') || (*previous >= 'a' && *previous <= 'z') || (*previous >= '0' && *previous <= '9') || *previous == '_') && start.backward_char()) {
    }
    while(((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') || (*end >= '0' && *end <= '9') || *end == '_') && end.forward_char()) {
    }

    if(start == end) {
      Info::get().print("No valid symbol found at current cursor location");
      return "";
    }

    return get_buffer()->get_text(start, end);
  };

  if(capabilities.rename || capabilities.document_highlight) {
    rename_similar_tokens = [this](const std::string &text) {
      class Changes {
      public:
        std::string file;
        std::vector<LanguageProtocol::TextEdit> text_edits;
      };

      auto previous_text = get_token_spelling();
      if(previous_text.empty())
        return;

      auto iter = get_buffer()->get_insert()->get_iter();
      std::vector<Changes> changes;
      std::promise<void> result_processed;
      if(capabilities.rename) {
        client->write_request(this, "textDocument/rename", R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "newName": ")" + text + "\"", [this, &changes, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            boost::filesystem::path project_path;
            auto build = Project::Build::create(file_path);
            if(!build->project_path.empty())
              project_path = build->project_path;
            else
              project_path = file_path.parent_path();
            try {
              auto changes_it = result.find("changes");
              if(changes_it != result.not_found()) {
                for(auto file_it = changes_it->second.begin(); file_it != changes_it->second.end(); ++file_it) {
                  auto file = file_it->first;
                  file.erase(0, 7);
                  if(filesystem::file_in_path(file, project_path)) {
                    std::vector<LanguageProtocol::TextEdit> edits;
                    for(auto edit_it = file_it->second.begin(); edit_it != file_it->second.end(); ++edit_it)
                      edits.emplace_back(edit_it->second);
                    changes.emplace_back(Changes{std::move(file), std::move(edits)});
                  }
                }
              }
              else {
                auto changes_pt = result.get_child("documentChanges", boost::property_tree::ptree());
                for(auto change_it = changes_pt.begin(); change_it != changes_pt.end(); ++change_it) {
                  auto document_it = change_it->second.find("textDocument");
                  if(document_it != change_it->second.not_found()) {
                    auto file = filesystem::get_path_from_uri(document_it->second.get<std::string>("uri", ""));
                    if(filesystem::file_in_path(file, project_path)) {
                      std::vector<LanguageProtocol::TextEdit> edits;
                      auto edits_pt = change_it->second.get_child("edits", boost::property_tree::ptree());
                      for(auto edit_it = edits_pt.begin(); edit_it != edits_pt.end(); ++edit_it)
                        edits.emplace_back(edit_it->second);
                      changes.emplace_back(Changes{file.string(), std::move(edits)});
                    }
                  }
                }
              }
            }
            catch(...) {
              changes.clear();
            }
          }
          result_processed.set_value();
        });
      }
      else {
        client->write_request(this, "textDocument/documentHighlight", R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "context": {"includeDeclaration": true})", [this, &changes, &text, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            try {
              std::vector<LanguageProtocol::TextEdit> edits;
              for(auto it = result.begin(); it != result.end(); ++it)
                edits.emplace_back(it->second, text);
              changes.emplace_back(Changes{file_path.string(), std::move(edits)});
            }
            catch(...) {
              changes.clear();
            }
          }
          result_processed.set_value();
        });
      }
      result_processed.get_future().get();

      std::vector<Changes *> changes_renamed;
      for(auto &change : changes) {
        auto view_it = views.end();
        for(auto it = views.begin(); it != views.end(); ++it) {
          if((*it)->file_path == change.file) {
            view_it = it;
            break;
          }
        }
        if(view_it != views.end()) {
          auto buffer = (*view_it)->get_buffer();
          buffer->begin_user_action();

          auto end_iter = buffer->end();
          // If entire buffer is replaced
          if(change.text_edits.size() == 1 &&
             change.text_edits[0].range.start.line == 0 && change.text_edits[0].range.start.character == 0 &&
             (change.text_edits[0].range.end.line > end_iter.get_line() ||
              (change.text_edits[0].range.end.line == end_iter.get_line() && change.text_edits[0].range.end.character >= end_iter.get_line_offset())))
            (*view_it)->replace_text(change.text_edits[0].new_text);
          else {
            for(auto edit_it = change.text_edits.rbegin(); edit_it != change.text_edits.rend(); ++edit_it) {
              auto start_iter = (*view_it)->get_iter_at_line_pos(edit_it->range.start.line, edit_it->range.start.character);
              auto end_iter = (*view_it)->get_iter_at_line_pos(edit_it->range.end.line, edit_it->range.end.character);
              buffer->erase(start_iter, end_iter);
              start_iter = (*view_it)->get_iter_at_line_pos(edit_it->range.start.line, edit_it->range.start.character);
              buffer->insert(start_iter, edit_it->new_text);
            }
          }

          buffer->end_user_action();
          (*view_it)->save();
          changes_renamed.emplace_back(&change);
        }
        else {
          Glib::ustring buffer;
          {
            std::ifstream stream(change.file, std::ifstream::binary);
            if(stream)
              buffer.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
          }
          std::ofstream stream(change.file, std::ifstream::binary);
          if(!buffer.empty() && stream) {
            std::vector<size_t> lines_start_pos = {0};
            for(size_t c = 0; c < buffer.size(); ++c) {
              if(buffer[c] == '\n')
                lines_start_pos.emplace_back(c + 1);
            }
            for(auto edit_it = change.text_edits.rbegin(); edit_it != change.text_edits.rend(); ++edit_it) {
              auto start_line = edit_it->range.start.line;
              auto end_line = edit_it->range.end.line;
              if(static_cast<size_t>(start_line) < lines_start_pos.size()) {
                auto start = lines_start_pos[start_line] + edit_it->range.start.character;
                size_t end;
                if(static_cast<size_t>(end_line) >= lines_start_pos.size())
                  end = buffer.size();
                else
                  end = lines_start_pos[end_line] + edit_it->range.end.character;
                if(start < buffer.size() && end <= buffer.size()) {
                  buffer.replace(start, end - start, edit_it->new_text);
                }
              }
            }
            stream.write(buffer.data(), buffer.bytes());
            changes_renamed.emplace_back(&change);
          }
          else
            Terminal::get().print("Error: could not write to file " + change.file + '\n', true);
        }
      }

      if(!changes_renamed.empty()) {
        Terminal::get().print("Renamed ");
        Terminal::get().print(previous_text, true);
        Terminal::get().print(" to ");
        Terminal::get().print(text, true);
        Terminal::get().print("\n");
      }
    };
  }

  if(capabilities.document_symbol) {
    get_methods = [this]() {
      std::vector<std::pair<Offset, std::string>> methods;

      std::promise<void> result_processed;
      client->write_request(this, "textDocument/documentSymbol", R"("textDocument":{"uri":")" + uri + "\"}", [&result_processed, &methods](const boost::property_tree::ptree &result, bool error) {
        if(!error) {
          for(auto it = result.begin(); it != result.end(); ++it) {
            try {
              auto kind = it->second.get<int>("kind");
              if(kind == 6 || kind == 9 || kind == 12) {
                LanguageProtocol::Location location(it->second.get_child("location"));

                std::string row;
                auto container_name = it->second.get<std::string>("containerName", "");
                if(!container_name.empty() && container_name != "null")
                  row += container_name + ':';
                row += std::to_string(location.range.start.line + 1) + ": <b>" + it->second.get<std::string>("name") + "</b>";

                methods.emplace_back(Offset(location.range.start.line, location.range.start.character), std::move(row));
              }
            }
            catch(...) {
            }
          }
        }
        result_processed.set_value();
      });
      result_processed.get_future().get();

      std::sort(methods.begin(), methods.end(), [](const std::pair<Offset, std::string> &a, const std::pair<Offset, std::string> &b) {
        return a.first < b.first;
      });
      return methods;
    };
  }

  goto_next_diagnostic = [this]() {
    place_cursor_at_next_diagnostic();
  };
}

void Source::LanguageProtocolView::escape_text(std::string &text) {
  for(size_t c = 0; c < text.size(); ++c) {
    if(text[c] == '\n') {
      text.replace(c, 1, "\\n");
      ++c;
    }
    else if(text[c] == '\r') {
      text.replace(c, 1, "\\r");
      ++c;
    }
    else if(text[c] == '\t') {
      text.replace(c, 1, "\\t");
      ++c;
    }
    else if(text[c] == '"') {
      text.replace(c, 1, "\\\"");
      ++c;
    }
    else if(text[c] == '\\') {
      text.replace(c, 1, "\\\\");
      ++c;
    }
  }
}

void Source::LanguageProtocolView::update_diagnostics(std::vector<LanguageProtocol::Diagnostic> &&diagnostics) {
  dispatcher.post([this, diagnostics = std::move(diagnostics)]() {
    diagnostic_offsets.clear();
    diagnostic_tooltips.clear();
    if(!capabilities.type_coverage)
      get_buffer()->remove_tag_by_name("def:warning_underline", get_buffer()->begin(), get_buffer()->end());
    get_buffer()->remove_tag_by_name("def:error_underline", get_buffer()->begin(), get_buffer()->end());
    if(!capabilities.type_coverage)
      num_warnings = 0;
    num_errors = 0;
    num_fix_its = 0;
    for(auto &diagnostic : diagnostics) {
      auto start = get_iter_at_line_pos(diagnostic.range.start.line, diagnostic.range.start.character);
      auto end = get_iter_at_line_pos(diagnostic.range.end.line, diagnostic.range.end.character);

      if(start == end) {
        if(!end.ends_line())
          end.forward_char();
        else
          while(start.ends_line() && start.backward_char()) { // Move start so that diagnostic underline is visible
          }
      }

      bool error = false;
      std::string severity_tag_name;
      if(diagnostic.severity >= 2) {
        severity_tag_name = "def:warning";
        num_warnings++;
      }
      else {
        severity_tag_name = "def:error";
        num_errors++;
        error = true;
      }

      add_diagnostic_tooltip(start, end, error, [this, diagnostic = std::move(diagnostic)](const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
        buffer->insert_at_cursor(diagnostic.message);

        for(size_t i = 0; i < diagnostic.related_informations.size(); ++i) {
          auto link = filesystem::get_relative_path(diagnostic.related_informations[i].location.file, file_path.parent_path()).string();
          link += ':' + std::to_string(diagnostic.related_informations[i].location.range.start.line + 1);
          link += ':' + std::to_string(diagnostic.related_informations[i].location.range.start.character + 1);

          if(i == 0)
            buffer->insert_at_cursor("\n\n");
          buffer->insert_at_cursor(diagnostic.related_informations[i].message);
          buffer->insert_at_cursor(": ");
          auto pos = buffer->get_insert()->get_iter();
          buffer->insert_with_tag(pos, link, link_tag);
          if(i != diagnostic.related_informations.size() - 1)
            buffer->insert_at_cursor("\n");
        }
      });
    }

    for(auto &mark : type_coverage_marks)
      add_diagnostic_tooltip(mark.first->get_iter(), mark.second->get_iter(), false, [](const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
        buffer->insert_at_cursor(type_coverage_message);
      });

    status_diagnostics = std::make_tuple(num_warnings, num_errors, num_fix_its);
    if(update_status_diagnostics)
      update_status_diagnostics(this);
  });
}

Gtk::TextIter Source::LanguageProtocolView::get_iter_at_line_pos(int line, int pos) {
  return get_iter_at_line_offset(line, pos);
}

void Source::LanguageProtocolView::show_type_tooltips(const Gdk::Rectangle &rectangle) {
  if(!capabilities.hover)
    return;

  Gtk::TextIter iter;
  int location_x, location_y;
  window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, rectangle.get_x(), rectangle.get_y(), location_x, location_y);
  location_x += (rectangle.get_width() - 1) / 2;
  get_iter_at_location(iter, location_x, location_y);
  Gdk::Rectangle iter_rectangle;
  get_iter_location(iter, iter_rectangle);
  if(iter.ends_line() && location_x > iter_rectangle.get_x())
    return;

  auto offset = iter.get_offset();

  static int request_count = 0;
  request_count++;
  auto current_request = request_count;
  client->write_request(this, "textDocument/hover", R"("textDocument": {"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + "}", [this, offset, current_request](const boost::property_tree::ptree &result, bool error) {
    if(!error) {
      // hover result structure vary significantly from the different language servers
      auto content = std::make_shared<std::string>();
      auto contents_pt = result.get_child("contents", boost::property_tree::ptree());
      for(auto it = contents_pt.begin(); it != contents_pt.end(); ++it) {
        auto value = it->second.get<std::string>("value", "");
        if(!value.empty())
          content->insert(0, value + (content->empty() ? "" : "\n\n"));
        else {
          value = it->second.get_value<std::string>("");
          if(!value.empty())
            *content += (content->empty() ? "" : "\n\n") + value;
        }
      }
      if(content->empty()) {
        auto contents_it = result.find("contents");
        if(contents_it != result.not_found()) {
          *content = contents_it->second.get<std::string>("value", "");
          if(content->empty())
            *content = contents_it->second.get_value<std::string>("");
        }
      }
      if(!content->empty()) {
        while(!content->empty() && content->back() == '\n') {
          content->pop_back();
        } // Remove unnecessary newlines
        dispatcher.post([this, offset, content, current_request] {
          if(current_request != request_count)
            return;
          if(Notebook::get().get_current_view() != this)
            return;
          if(offset >= get_buffer()->get_char_count())
            return;
          type_tooltips.clear();

          auto start = get_buffer()->get_iter_at_offset(offset);
          auto end = start;
          auto previous = start;
          while(previous.backward_char() && ((*previous >= 'A' && *previous <= 'Z') || (*previous >= 'a' && *previous <= 'z') || (*previous >= '0' && *previous <= '9') || *previous == '_') && start.backward_char()) {
          }
          while(((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') || (*end >= '0' && *end <= '9') || *end == '_') && end.forward_char()) {
          }
          type_tooltips.emplace_back(this, get_buffer()->create_mark(start), get_buffer()->create_mark(end), [this, offset, content](const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
            insert_with_links_tagged(buffer, *content);

#ifdef JUCI_ENABLE_DEBUG
            if(language_id == "rust" && capabilities.definition) {
              if(Debug::LLDB::get().is_stopped()) {
                Glib::ustring value_type = "Value";

                auto start = get_buffer()->get_iter_at_offset(offset);
                auto end = start;
                auto previous = start;
                while(previous.backward_char() && ((*previous >= 'A' && *previous <= 'Z') || (*previous >= 'a' && *previous <= 'z') || (*previous >= '0' && *previous <= '9') || *previous == '_') && start.backward_char()) {
                }
                while(((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') || (*end >= '0' && *end <= '9') || *end == '_') && end.forward_char()) {
                }
                auto variable = get_buffer()->get_text(start, end);

                auto offset = get_declaration(start);
                Glib::ustring debug_value = Debug::LLDB::get().get_value(variable, offset.file_path, offset.line + 1, offset.index + 1);
                if(debug_value.empty()) {
                  value_type = "Return value";
                  debug_value = Debug::LLDB::get().get_return_value(file_path, start.get_line() + 1, start.get_line_index() + 1);
                }
                if(!debug_value.empty()) {
                  size_t pos = debug_value.find(" = ");
                  if(pos != Glib::ustring::npos) {
                    Glib::ustring::iterator iter;
                    while(!debug_value.validate(iter)) {
                      auto next_char_iter = iter;
                      next_char_iter++;
                      debug_value.replace(iter, next_char_iter, "?");
                    }
                    buffer->insert(buffer->get_insert()->get_iter(), "\n\n" + value_type + ": " + debug_value.substr(pos + 3, debug_value.size() - (pos + 3) - 1));
                  }
                }
              }
            }
#endif
          });
          type_tooltips.show();
        });
      }
    }
  });
}

void Source::LanguageProtocolView::apply_similar_symbol_tag() {
  if(!capabilities.document_highlight && !capabilities.references)
    return;

  auto iter = get_buffer()->get_insert()->get_iter();
  std::string method;
  if(capabilities.document_highlight)
    method = "textDocument/documentHighlight";
  else
    method = "textDocument/references";

  static int request_count = 0;
  request_count++;
  auto current_request = request_count;
  client->write_request(this, method, R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "context": {"includeDeclaration": true})", [this, current_request](const boost::property_tree::ptree &result, bool error) {
    if(!error) {
      std::vector<LanguageProtocol::Range> ranges;
      for(auto it = result.begin(); it != result.end(); ++it) {
        try {
          if(capabilities.document_highlight || it->second.get<std::string>("uri") == uri)
            ranges.emplace_back(it->second.get_child("range"));
        }
        catch(...) {
        }
      }
      dispatcher.post([this, ranges = std::move(ranges), current_request] {
        if(current_request != request_count || !similar_symbol_tag_applied)
          return;
        get_buffer()->remove_tag(similar_symbol_tag, get_buffer()->begin(), get_buffer()->end());
        for(auto &range : ranges) {
          auto start = get_iter_at_line_pos(range.start.line, range.start.character);
          auto end = get_iter_at_line_pos(range.end.line, range.end.character);
          get_buffer()->apply_tag(similar_symbol_tag, start, end);
        }
      });
    }
  });
}

void Source::LanguageProtocolView::apply_clickable_tag(const Gtk::TextIter &iter) {
  static int request_count = 0;
  request_count++;
  auto current_request = request_count;
  auto line = iter.get_line();
  auto offset = iter.get_line_offset();
  client->write_request(this, "textDocument/definition", R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(line) + ", \"character\": " + std::to_string(offset) + "}", [this, current_request, line, offset](const boost::property_tree::ptree &result, bool error) {
    if(!error && !result.empty()) {
      dispatcher.post([this, current_request, line, offset] {
        if(current_request != request_count || !clickable_tag_applied)
          return;
        get_buffer()->remove_tag(clickable_tag, get_buffer()->begin(), get_buffer()->end());
        auto range = get_token_iters(get_iter_at_line_offset(line, offset));
        get_buffer()->apply_tag(clickable_tag, range.first, range.second);
      });
    }
  });
}

Source::Offset Source::LanguageProtocolView::get_declaration(const Gtk::TextIter &iter) {
  auto offset = std::make_shared<Offset>();
  std::promise<void> result_processed;
  client->write_request(this, "textDocument/definition", R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + "}", [offset, &result_processed](const boost::property_tree::ptree &result, bool error) {
    if(!error) {
      for(auto it = result.begin(); it != result.end(); ++it) {
        try {
          LanguageProtocol::Location location(it->second);
          offset->file_path = std::move(location.file);
          offset->line = location.range.start.line;
          offset->index = location.range.start.character;
          break; // TODO: can a language server return several definitions?
        }
        catch(...) {
        }
      }
    }
    result_processed.set_value();
  });
  result_processed.get_future().get();
  return *offset;
}

void Source::LanguageProtocolView::setup_autocomplete() {
  if(!capabilities.completion)
    return;

  non_interactive_completion = [this] {
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
      return;
    autocomplete.run();
  };

  autocomplete.reparse = [this] {
    autocomplete_comment.clear();
    autocomplete_insert.clear();
  };

  if(capabilities.signature_help) {
    // Activate argument completions
    get_buffer()->signal_changed().connect([this] {
      if(!interactive_completion)
        return;
      if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
        return;
      if(!has_focus())
        return;
      if(autocomplete_show_parameters)
        autocomplete.stop();
      autocomplete_show_parameters = false;
      autocomplete_delayed_show_arguments_connection.disconnect();
      autocomplete_delayed_show_arguments_connection = Glib::signal_timeout().connect([this]() {
        if(get_buffer()->get_has_selection())
          return false;
        if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
          return false;
        if(!has_focus())
          return false;
        if(is_possible_argument()) {
          autocomplete.stop();
          autocomplete.run();
        }
        return false;
      }, 500);
    }, false);

    // Remove argument completions
    if(!has_named_parameters()) { // Do not remove named parameters in for instance Python
      signal_key_press_event().connect([this](GdkEventKey *key) {
        if(autocomplete_show_parameters && CompletionDialog::get() && CompletionDialog::get()->is_visible() &&
           key->keyval != GDK_KEY_Down && key->keyval != GDK_KEY_Up &&
           key->keyval != GDK_KEY_Return && key->keyval != GDK_KEY_KP_Enter &&
           key->keyval != GDK_KEY_ISO_Left_Tab && key->keyval != GDK_KEY_Tab &&
           (key->keyval < GDK_KEY_Shift_L || key->keyval > GDK_KEY_Hyper_R)) {
          get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());
          CompletionDialog::get()->hide();
        }
        return false;
      }, false);
    }
  }

  autocomplete.is_continue_key = [](guint keyval) {
    if((keyval >= '0' && keyval <= '9') || (keyval >= 'a' && keyval <= 'z') || (keyval >= 'A' && keyval <= 'Z') || keyval == '_')
      return true;

    return false;
  };

  autocomplete.is_restart_key = [this](guint keyval) {
    auto iter = get_buffer()->get_insert()->get_iter();
    iter.backward_chars(2);
    if(keyval == '.' || (keyval == ':' && *iter == ':'))
      return true;
    return false;
  };

  autocomplete.run_check = [this]() {
    auto iter = get_buffer()->get_insert()->get_iter();
    iter.backward_char();
    if(!is_code_iter(iter))
      return false;

    autocomplete_enable_snippets = false;
    autocomplete_show_parameters = false;

    auto line = ' ' + get_line_before();
    const static std::regex regex("^.*([a-zA-Z_\\)\\]\\>\"']|[^a-zA-Z0-9_][a-zA-Z_][a-zA-Z0-9_]*)(\\.)([a-zA-Z0-9_]*)$|" // .
                                  "^.*(::)([a-zA-Z0-9_]*)$|"                                                             // ::
                                  "^.*[^a-zA-Z0-9_]([a-zA-Z_][a-zA-Z0-9_]{2,})$");                                       // part of symbol
    std::smatch sm;
    if(std::regex_match(line, sm, regex)) {
      {
        std::lock_guard<std::mutex> lock(autocomplete.prefix_mutex);
        autocomplete.prefix = sm.length(2) ? sm[3].str() : sm.length(4) ? sm[5].str() : sm[6].str();
        if(!sm.length(2) && !sm.length(4))
          autocomplete_enable_snippets = true;
      }
      return true;
    }
    else if(is_possible_argument()) {
      autocomplete_show_parameters = true;
      std::lock_guard<std::mutex> lock(autocomplete.prefix_mutex);
      autocomplete.prefix = "";
      return true;
    }
    else if(!interactive_completion) {
      auto end_iter = get_buffer()->get_insert()->get_iter();
      auto iter = end_iter;
      while(iter.backward_char() && autocomplete.is_continue_key(*iter)) {
      }
      if(iter != end_iter)
        iter.forward_char();

      {
        std::lock_guard<std::mutex> lock(autocomplete.prefix_mutex);
        autocomplete.prefix = get_buffer()->get_text(iter, end_iter);
      }
      auto prev1 = iter;
      if(prev1.backward_char() && *prev1 != '.') {
        auto prev2 = prev1;
        if(!prev2.backward_char())
          autocomplete_enable_snippets = true;
        else if(!(*prev2 == ':' && *prev1 == ':'))
          autocomplete_enable_snippets = true;
      }

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
    if(autocomplete.state == Autocomplete::State::STARTING) {
      autocomplete_comment.clear();
      autocomplete_insert.clear();
      std::promise<void> result_processed;
      if(autocomplete_show_parameters) {
        if(!capabilities.signature_help)
          return;
        client->write_request(this, "textDocument/signatureHelp", R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(line_number - 1) + ", \"character\": " + std::to_string(column - 1) + "}", [this, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            auto signatures = result.get_child("signatures", boost::property_tree::ptree());
            for(auto signature_it = signatures.begin(); signature_it != signatures.end(); ++signature_it) {
              auto parameters = signature_it->second.get_child("parameters", boost::property_tree::ptree());
              for(auto parameter_it = parameters.begin(); parameter_it != parameters.end(); ++parameter_it) {
                auto label = parameter_it->second.get<std::string>("label", "");
                auto insert = label;
                auto documentation = parameter_it->second.get<std::string>("documentation", "");
                autocomplete.rows.emplace_back(std::move(label));
                autocomplete_insert.emplace_back(std::move(insert));
                autocomplete_comment.emplace_back(std::move(documentation));
              }
            }
          }
          result_processed.set_value();
        });
      }
      else {
        client->write_request(this, "textDocument/completion", R"("textDocument":{"uri":")" + uri + R"("}, "position": {"line": )" + std::to_string(line_number - 1) + ", \"character\": " + std::to_string(column - 1) + "}", [this, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            auto begin = result.begin(); // rust language server is bugged
            auto end = result.end();
            auto items_it = result.find("items"); // correct
            if(items_it != result.not_found()) {
              begin = items_it->second.begin();
              end = items_it->second.end();
            }
            for(auto it = begin; it != end; ++it) {
              auto label = it->second.get<std::string>("label", "");
              auto detail = it->second.get<std::string>("detail", "");
              auto documentation = it->second.get<std::string>("documentation", "");
              auto insert = it->second.get<std::string>("insertText", "");
              if(insert.empty())
                insert = it->second.get<std::string>("textEdit.newText", "");
              if(!insert.empty()) {
                // In case ( is missing in insert but is present in label
                if(label.size() > insert.size() && label.back() == ')' && insert.find('(') == std::string::npos) {
                  auto pos = label.find('(');
                  if(pos != std::string::npos && pos == insert.size() && pos + 1 < label.size()) {
                    if(pos + 2 == label.size()) // If no parameters
                      insert += "()";
                    else
                      insert += "(${1:" + label.substr(pos + 1, label.size() - 1 - (pos + 1)) + "})";
                  }
                }
              }
              else {
                insert = label;
                auto kind = it->second.get<int>("kind", 0);
                if(kind >= 2 && kind <= 3) {
                  bool found_bracket = false;
                  for(auto &chr : insert) {
                    if(chr == '(' || chr == '{') {
                      found_bracket = true;
                      break;
                    }
                  }
                  if(!found_bracket)
                    insert += "(${1:})";
                }
              }
              if(!label.empty()) {
                std::string prefix;
                {
                  std::lock_guard<std::mutex> lock(autocomplete.prefix_mutex);
                  prefix = autocomplete.prefix;
                }
                if(prefix.compare(0, prefix.size(), label, 0, prefix.size()) == 0) {
                  autocomplete.rows.emplace_back(std::move(label));
                  autocomplete_comment.emplace_back(std::move(detail));
                  if(!documentation.empty() && documentation != autocomplete_comment.back()) {
                    if(!autocomplete_comment.back().empty())
                      autocomplete_comment.back() += "\n\n";
                    autocomplete_comment.back() += documentation;
                  }
                  autocomplete_insert.emplace_back(std::move(insert));
                }
              }
            }

            if(autocomplete_enable_snippets) {
              std::string prefix;
              {
                std::lock_guard<std::mutex> lock(autocomplete.prefix_mutex);
                prefix = autocomplete.prefix;
              }
              std::lock_guard<std::mutex> lock(snippets_mutex);
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
          }
          result_processed.set_value();
        });
      }
      result_processed.get_future().get();
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

    // Do not insert function/template parameters if they already exist
    auto iter = get_buffer()->get_insert()->get_iter();
    if(*iter == '(' || *iter == '<') {
      auto bracket_pos = insert.find(*iter);
      if(bracket_pos != std::string::npos) {
        insert = insert.substr(0, bracket_pos);
      }
    }

    if(hide_window) {
      if(autocomplete_show_parameters) {
        if(has_named_parameters()) // Do not select named parameters in for instance Python
          get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert);
        else {
          get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert);
          int start_offset = CompletionDialog::get()->start_mark->get_iter().get_offset();
          int end_offset = CompletionDialog::get()->start_mark->get_iter().get_offset() + insert.size();
          get_buffer()->select_range(get_buffer()->get_iter_at_offset(start_offset), get_buffer()->get_iter_at_offset(end_offset));
        }
        return;
      }

      insert_snippet(CompletionDialog::get()->start_mark->get_iter(), insert);
    }
    else
      get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert);
  };

  autocomplete.get_tooltip = [this](unsigned int index) {
    return autocomplete_comment[index];
  };
}

bool Source::LanguageProtocolView::has_named_parameters() {
  if(language_id == "python") // TODO: add more languages that supports named parameters
    return true;
  return false;
}

void Source::LanguageProtocolView::update_type_coverage() {
  if(capabilities.type_coverage) {
    client->write_request(this, "textDocument/typeCoverage", R"("textDocument": {"uri":")" + uri + "\"}", [this](const boost::property_tree::ptree &result, bool error) {
      if(error) {
        if(update_type_coverage_retries > 0) { // Retry typeCoverage request, since these requests can fail while waiting for language server to start
          dispatcher.post([this] {
            update_type_coverage_connection.disconnect();
            update_type_coverage_connection = Glib::signal_timeout().connect([this]() {
              --update_type_coverage_retries;
              update_type_coverage();
              return false;
            }, 1000);
          });
        }
        return;
      }
      update_type_coverage_retries = 0;

      std::vector<LanguageProtocol::Range> ranges;
      auto uncoveredRanges = result.get_child("uncoveredRanges", boost::property_tree::ptree());
      for(auto it = uncoveredRanges.begin(); it != uncoveredRanges.end(); ++it) {
        try {
          ranges.emplace_back(it->second.get_child("range"));
        }
        catch(...) {
        }
      }

      dispatcher.post([this, ranges = std::move(ranges)] {
        num_warnings = 0;
        for(auto &mark : type_coverage_marks) {
          get_buffer()->delete_mark(mark.first);
          get_buffer()->delete_mark(mark.second);
        }
        type_coverage_marks.clear();
        get_buffer()->remove_tag_by_name("def:warning_underline", get_buffer()->begin(), get_buffer()->end());
        for(auto &range : ranges) {
          auto start = get_iter_at_line_offset(range.start.line, range.start.character);
          auto end = get_iter_at_line_offset(range.end.line, range.end.character);
          add_diagnostic_tooltip(start, end, false, [](const Glib::RefPtr<Gtk::TextBuffer> &buffer) {
            buffer->insert_at_cursor(type_coverage_message);
          });
          type_coverage_marks.emplace_back(get_buffer()->create_mark(start), get_buffer()->create_mark(end));

          ++num_warnings;
        }

        status_diagnostics = std::make_tuple(num_warnings, num_errors, num_fix_its);
        if(update_status_diagnostics)
          update_status_diagnostics(this);
      });
    });
  }
}
