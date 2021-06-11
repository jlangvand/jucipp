#include "source_language_protocol.hpp"
#include "filesystem.hpp"
#include "info.hpp"
#include "notebook.hpp"
#include "project.hpp"
#include "selection_dialog.hpp"
#include "terminal.hpp"
#ifdef JUCI_ENABLE_DEBUG
#include "debug_lldb.hpp"
#endif
#include "config.hpp"
#include "menu.hpp"
#include "utility.hpp"
#include <future>
#include <limits>
#include <regex>
#include <unordered_map>

const std::string type_coverage_message = "Un-type checked code. Consider adding type annotations.";

LanguageProtocol::Offset::Offset(const boost::property_tree::ptree &pt) {
  try {
    line = pt.get<int>("line");
    character = pt.get<int>("character");
  }
  catch(...) {
    // Workaround for buggy rls
    line = std::min(pt.get<std::size_t>("line"), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    character = std::min(pt.get<std::size_t>("character"), static_cast<std::size_t>(std::numeric_limits<int>::max()));
  }
}
LanguageProtocol::Range::Range(const boost::property_tree::ptree &pt) : start(pt.get_child("start")), end(pt.get_child("end")) {}

LanguageProtocol::Location::Location(const boost::property_tree::ptree &pt, std::string file_) : range(pt.get_child("range")) {
  if(file_.empty()) {
    file = filesystem::get_path_from_uri(pt.get<std::string>("uri")).string();
  }
  else
    file = std::move(file_);
}

LanguageProtocol::Diagnostic::RelatedInformation::RelatedInformation(const boost::property_tree::ptree &pt) : message(pt.get<std::string>("message")), location(pt.get_child("location")) {}

LanguageProtocol::Diagnostic::Diagnostic(const boost::property_tree::ptree &pt) : message(pt.get<std::string>("message")), range(pt.get_child("range")), severity(pt.get<int>("severity", 0)), code(pt.get<std::string>("code", "")) {
  auto related_information_it = pt.get_child("relatedInformation", boost::property_tree::ptree());
  for(auto it = related_information_it.begin(); it != related_information_it.end(); ++it)
    related_informations.emplace_back(it->second);
}

LanguageProtocol::TextEdit::TextEdit(const boost::property_tree::ptree &pt, std::string new_text_) : range(pt.get_child("range")), new_text(new_text_.empty() ? pt.get<std::string>("newText") : std::move(new_text_)) {}

LanguageProtocol::TextDocumentEdit::TextDocumentEdit(const boost::property_tree::ptree &pt) : file(filesystem::get_path_from_uri(pt.get<std::string>("textDocument.uri", "")).string()) {
  auto edits_it = pt.get_child("edits", boost::property_tree::ptree());
  for(auto it = edits_it.begin(); it != edits_it.end(); ++it)
    edits.emplace_back(it->second);
}

std::string LanguageProtocol::escape_text(std::string text) {
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
  return text;
}

LanguageProtocol::Client::Client(boost::filesystem::path root_path_, std::string language_id_, const std::string &language_server) : root_path(std::move(root_path_)), language_id(std::move(language_id_)) {
  process = std::make_unique<TinyProcessLib::Process>(
      filesystem::escape_argument(language_server), root_path.string(),
      [this](const char *bytes, size_t n) {
        server_message_stream.write(bytes, n);
        parse_server_message();
      },
      [](const char *bytes, size_t n) {
        std::cerr.write(bytes, n);
      },
      true, TinyProcessLib::Config{1048576});
}

std::shared_ptr<LanguageProtocol::Client> LanguageProtocol::Client::get(const boost::filesystem::path &file_path, const std::string &language_id, const std::string &language_server) {
  boost::filesystem::path root_path;
  auto build = Project::Build::create(file_path);
  if(!build->project_path.empty())
    root_path = build->project_path;
  else
    root_path = file_path.parent_path();

  auto cache_id = root_path.string() + '|' + language_id;

  static Mutex mutex;
  static std::unordered_map<std::string, std::weak_ptr<Client>> cache GUARDED_BY(mutex);

  LockGuard lock(mutex);
  auto it = cache.find(cache_id);
  if(it == cache.end())
    it = cache.emplace(cache_id, std::weak_ptr<Client>()).first;
  auto instance = it->second.lock();
  if(!instance)
    it->second = instance = std::shared_ptr<Client>(new Client(root_path, language_id, language_server), [](Client *client_ptr) {
      std::thread delete_thread([client_ptr] { // Delete client in the background
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

  LockGuard lock(timeout_threads_mutex);
  for(auto &thread : timeout_threads)
    thread.join();

  int exit_status = -1;
  for(size_t c = 0; c < 10; ++c) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if(process->try_get_exit_status(exit_status))
      break;
  }
  if(exit_status == -1) {
    process->kill();
    exit_status = process->get_exit_status();
  }

  if(on_exit_status)
    on_exit_status(exit_status);
  if(Config::get().log.language_server)
    std::cout << "Language server exit status: " << exit_status << std::endl;
}

boost::optional<LanguageProtocol::Capabilities> LanguageProtocol::Client::get_capabilities(Source::LanguageProtocolView *view) {
  if(view) {
    LockGuard lock(views_mutex);
    views.emplace(view);
  }

  LockGuard lock(initialize_mutex);
  if(initialized)
    return capabilities;
  return {};
}

LanguageProtocol::Capabilities LanguageProtocol::Client::initialize(Source::LanguageProtocolView *view) {
  if(view) {
    LockGuard lock(views_mutex);
    views.emplace(view);
  }

  LockGuard lock(initialize_mutex);

  if(initialized)
    return capabilities;

  std::promise<void> result_processed;
  TinyProcessLib::Process::id_type process_id;
  {
    LockGuard lock(read_write_mutex);
    process_id = process->get_id();
  }
  write_request(
      nullptr, "initialize", "\"processId\":" + std::to_string(process_id) + R"(,"rootUri":")" + LanguageProtocol::escape_text(filesystem::get_uri_from_path(root_path)) + R"(","capabilities": {
  "workspace": {
    "symbol": { "dynamicRegistration": false }
  },
  "textDocument": {
    "synchronization": { "dynamicRegistration": false, "didSave": true },
    "completion": {
      "dynamicRegistration": false,
      "completionItem": {
        "snippetSupport": true,
        "documentationFormat": ["markdown", "plaintext"]
      }
    },
    "hover": {
      "dynamicRegistration": false,
      "contentFormat": ["markdown", "plaintext"]
    },
    "signatureHelp": {
      "dynamicRegistration": false,
      "signatureInformation": {
        "documentationFormat": ["markdown", "plaintext"]
      }
    },
    "definition": { "dynamicRegistration": false },
    "references": { "dynamicRegistration": false },
    "documentHighlight": { "dynamicRegistration": false },
    "documentSymbol": { "dynamicRegistration": false },
    "formatting": { "dynamicRegistration": false },
    "rangeFormatting": { "dynamicRegistration": false },
    "rename": { "dynamicRegistration": false },
    "publishDiagnostics": { "relatedInformation":true },
    "codeAction": {
      "dynamicRegistration": false,
      "codeActionLiteralSupport": {
        "codeActionKind": { "valueSet": ["quickfix"] }
      }
    }
  },
  "offsetEncoding": ["utf-8"]
},
"initializationOptions": {
  "checkOnSave": { "enable": true }
},
"trace": "off")",
      [this, &result_processed](const boost::property_tree::ptree &result, bool error) {
        if(!error) {
          if(auto capabilities_pt = result.get_child_optional("capabilities")) {
            try {
              capabilities.text_document_sync = static_cast<LanguageProtocol::Capabilities::TextDocumentSync>(capabilities_pt->get<int>("textDocumentSync"));
            }
            catch(...) {
              capabilities.text_document_sync = static_cast<LanguageProtocol::Capabilities::TextDocumentSync>(capabilities_pt->get<int>("textDocumentSync.change", 0));
            }
            capabilities.hover = capabilities_pt->get<bool>("hoverProvider", false);
            capabilities.completion = static_cast<bool>(capabilities_pt->get_child_optional("completionProvider"));
            capabilities.signature_help = static_cast<bool>(capabilities_pt->get_child_optional("signatureHelpProvider"));
            capabilities.definition = capabilities_pt->get<bool>("definitionProvider", false);
            capabilities.references = capabilities_pt->get<bool>("referencesProvider", false);
            capabilities.document_highlight = capabilities_pt->get<bool>("documentHighlightProvider", false);
            capabilities.workspace_symbol = capabilities_pt->get<bool>("workspaceSymbolProvider", false);
            capabilities.document_symbol = capabilities_pt->get<bool>("documentSymbolProvider", false);
            capabilities.document_formatting = capabilities_pt->get<bool>("documentFormattingProvider", false);
            capabilities.document_range_formatting = capabilities_pt->get<bool>("documentRangeFormattingProvider", false);
            capabilities.rename = capabilities_pt->get<bool>("renameProvider", false);
            if(!capabilities.rename)
              capabilities.rename = capabilities_pt->get<bool>("renameProvider.prepareProvider", false);
            capabilities.code_action = capabilities_pt->get<bool>("codeActionProvider", false);
            if(!capabilities.code_action)
              capabilities.code_action = static_cast<bool>(capabilities_pt->get_child_optional("codeActionProvider.codeActionKinds"));
            capabilities.type_coverage = capabilities_pt->get<bool>("typeCoverageProvider", false);
          }

          write_notification("initialized", "");
        }
        result_processed.set_value();
      });
  result_processed.get_future().get();

  initialized = true;
  return capabilities;
}

void LanguageProtocol::Client::close(Source::LanguageProtocolView *view) {
  {
    LockGuard lock(views_mutex);
    auto it = views.find(view);
    if(it != views.end())
      views.erase(it);
  }
  LockGuard lock(read_write_mutex);
  for(auto it = handlers.begin(); it != handlers.end();) {
    if(it->second.first == view) {
      auto function = std::move(it->second.second);
      it = handlers.erase(it);
      lock.unlock();
      function(boost::property_tree::ptree(), true);
      lock.lock();
    }
    else
      it++;
  }
}

void LanguageProtocol::Client::parse_server_message() {
  if(!header_read) {
    std::string line;
    while(!header_read && std::getline(server_message_stream, line)) {
      if(!line.empty() && line != "\r") {
        if(starts_with(line, "Content-Length: ")) {
          try {
            server_message_size = static_cast<size_t>(std::stoul(line.substr(16)));
          }
          catch(...) {
          }
        }
      }
      else if(server_message_size) {
        server_message_content_pos = server_message_stream.tellg();
        *server_message_size += server_message_content_pos;
        header_read = true;
      }
    }
  }

  if(header_read) {
    server_message_stream.seekg(0, std::ios::end);
    size_t read_size = server_message_stream.tellg();
    if(read_size >= *server_message_size) {
      std::stringstream tmp;
      if(read_size > *server_message_size) {
        server_message_stream.seekg(*server_message_size, std::ios::beg);
        server_message_stream.seekp(*server_message_size, std::ios::beg);
        for(size_t c = *server_message_size; c < read_size; ++c) {
          tmp.put(server_message_stream.get());
          server_message_stream.put(' ');
        }
      }

      try {
        server_message_stream.seekg(server_message_content_pos, std::ios::beg);
        boost::property_tree::ptree pt;
        boost::property_tree::read_json(server_message_stream, pt);

        if(Config::get().log.language_server) {
          std::cout << "language server: ";
          boost::property_tree::write_json(std::cout, pt);
        }

        auto message_id = pt.get_optional<size_t>("id");
        {
          LockGuard lock(read_write_mutex);
          if(auto result = pt.get_child_optional("result")) {
            if(message_id) {
              auto id_it = handlers.find(*message_id);
              if(id_it != handlers.end()) {
                auto function = std::move(id_it->second.second);
                handlers.erase(id_it);
                lock.unlock();
                function(*result, false);
                lock.lock();
              }
            }
          }
          else if(auto error = pt.get_child_optional("error")) {
            if(!Config::get().log.language_server)
              boost::property_tree::write_json(std::cerr, pt);
            if(message_id) {
              auto id_it = handlers.find(*message_id);
              if(id_it != handlers.end()) {
                auto function = std::move(id_it->second.second);
                handlers.erase(id_it);
                lock.unlock();
                function(*error, true);
                lock.lock();
              }
            }
          }
          else if(auto method = pt.get_optional<std::string>("method")) {
            if(auto params = pt.get_child_optional("params")) {
              lock.unlock();
              if(message_id)
                handle_server_request(*message_id, *method, *params);
              else
                handle_server_notification(*method, *params);
              lock.lock();
            }
          }
        }
      }
      catch(...) {
        Terminal::get().async_print("\e[31mError\e[m: failed to parse message from language server\n", true);
      }

      server_message_stream = std::stringstream();
      server_message_size.reset();
      header_read = false;

      tmp.seekg(0, std::ios::end);
      if(tmp.tellg() > 0) {
        tmp.seekg(0, std::ios::beg);
        server_message_stream = std::move(tmp);
        parse_server_message();
      }
    }
  }
}

void LanguageProtocol::Client::write_request(Source::LanguageProtocolView *view, const std::string &method, const std::string &params, std::function<void(const boost::property_tree::ptree &, bool error)> &&function) {
  LockGuard lock(read_write_mutex);
  if(function) {
    handlers.emplace(message_id, std::make_pair(view, std::move(function)));

    auto message_id = this->message_id;
    LockGuard lock(timeout_threads_mutex);
    timeout_threads.emplace_back([this, message_id] {
      for(size_t c = 0; c < 40; ++c) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500) * (language_id == "julia" ? 100 : 1));
        LockGuard lock(read_write_mutex);
        auto id_it = handlers.find(message_id);
        if(id_it == handlers.end())
          return;
      }
      LockGuard lock(read_write_mutex);
      auto id_it = handlers.find(message_id);
      if(id_it != handlers.end()) {
        Terminal::get().async_print("\e[33mWarning\e[m: request to language server timed out. If you suspect the server has crashed, please close and reopen all project source files.\n", true);
        auto function = std::move(id_it->second.second);
        handlers.erase(id_it);
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
    Terminal::get().async_print("\e[31mError\e[m: could not write to language server. Please close and reopen all project files.\n", true);
    auto id_it = handlers.find(message_id - 1);
    if(id_it != handlers.end()) {
      auto function = std::move(id_it->second.second);
      handlers.erase(id_it);
      lock.unlock();
      function(boost::property_tree::ptree(), true);
      lock.lock();
    }
  }
}

void LanguageProtocol::Client::write_response(size_t id, const std::string &result) {
  LockGuard lock(read_write_mutex);
  std::string content(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"result":{)" + result + "}}");
  auto message = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n" + content;
  if(Config::get().log.language_server)
    std::cout << "Language client: " << content << std::endl;
  process->write(message);
}

void LanguageProtocol::Client::write_notification(const std::string &method, const std::string &params) {
  LockGuard lock(read_write_mutex);
  std::string content(R"({"jsonrpc":"2.0","method":")" + method + R"(","params":{)" + params + "}}");
  auto message = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n" + content;
  if(Config::get().log.language_server)
    std::cout << "Language client: " << content << std::endl;
  process->write(message);
}

void LanguageProtocol::Client::handle_server_notification(const std::string &method, const boost::property_tree::ptree &params) {
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
      LockGuard lock(views_mutex);
      for(auto view : views) {
        if(file == view->file_path) {
          view->update_diagnostics_async(std::move(diagnostics));
          break;
        }
      }
    }
  }
}

void LanguageProtocol::Client::handle_server_request(size_t id, const std::string &method, const boost::property_tree::ptree &params) {
  // TODO: respond to requests from server here
  // write_response(*id, "");
}

Source::LanguageProtocolView::LanguageProtocolView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language, std::string language_id_, std::string language_server_)
    : Source::BaseView(file_path, language), Source::View(file_path, language), uri(filesystem::get_uri_from_path(file_path)), uri_escaped(LanguageProtocol::escape_text(uri)), language_id(std::move(language_id_)), language_server(std::move(language_server_)), client(LanguageProtocol::Client::get(file_path, language_id, language_server)) {
  initialize();
}

void Source::LanguageProtocolView::initialize() {
  status_diagnostics = std::make_tuple(0, 0, 0);
  if(update_status_diagnostics)
    update_status_diagnostics(this);

  status_state = "initializing...";
  if(update_status_state)
    update_status_state(this);

  set_editable(false);

  auto init = [this](const LanguageProtocol::Capabilities &capabilities) {
    this->capabilities = capabilities;
    set_editable(true);

    client->write_notification("textDocument/didOpen", R"("textDocument":{"uri":")" + uri_escaped + R"(","languageId":")" + language_id + R"(","version":)" + std::to_string(document_version++) + R"(,"text":")" + LanguageProtocol::escape_text(get_buffer()->get_text().raw()) + "\"}");

    if(!initialized) {
      setup_signals();
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
  };

  if(auto capabilities = client->get_capabilities(this))
    init(*capabilities);
  else {
    initialize_thread = std::thread([this, init] {
      auto capabilities = client->initialize(this);
      dispatcher.post([init, capabilities] {
        init(capabilities);
      });
    });
  }
}

void Source::LanguageProtocolView::close() {
  autocomplete_delayed_show_arguments_connection.disconnect();
  update_type_coverage_connection.disconnect();

  if(initialize_thread.joinable())
    initialize_thread.join();

  if(autocomplete) {
    autocomplete->state = Autocomplete::State::idle;
    if(autocomplete->thread.joinable())
      autocomplete->thread.join();
  }

  client->write_notification("textDocument/didClose", R"("textDocument":{"uri":")" + uri_escaped + "\"}");
  client->close(this);
  client = nullptr;
}

Source::LanguageProtocolView::~LanguageProtocolView() {
  close();
  thread_pool.shutdown(true);
}

void Source::LanguageProtocolView::rename(const boost::filesystem::path &path) {
  // Reset view
  close();
  dispatcher.reset();
  Source::DiffView::rename(path);
  uri = filesystem::get_uri_from_path(path);
  uri_escaped = LanguageProtocol::escape_text(uri);
  client = LanguageProtocol::Client::get(file_path, language_id, language_server);
  initialize();
}

bool Source::LanguageProtocolView::save() {
  if(!Source::View::save())
    return false;

  client->write_notification("textDocument/didSave", R"("textDocument":{"uri":")" + uri_escaped + "\"}");

  update_type_coverage();

  return true;
}

void Source::LanguageProtocolView::setup_navigation_and_refactoring() {
  if(capabilities.document_formatting && !(format_style && is_js /* Use Prettier instead */)) {
    format_style = [this](bool continue_without_style_file) {
      if(!continue_without_style_file) {
        bool has_style_file = false;
        auto style_file_search_path = file_path.parent_path();
        auto style_file = '.' + language_id + "-format";

        boost::system::error_code ec;
        while(true) {
          if(boost::filesystem::exists(style_file_search_path / style_file, ec)) {
            has_style_file = true;
            break;
          }
          if(style_file_search_path == style_file_search_path.root_directory())
            break;
          style_file_search_path = style_file_search_path.parent_path();
        }

        if(!has_style_file && language_id == "rust") {
          auto style_file_search_path = file_path.parent_path();
          while(true) {
            if(boost::filesystem::exists(style_file_search_path / "rustfmt.toml", ec) ||
               boost::filesystem::exists(style_file_search_path / ".rustfmt.toml", ec)) {
              has_style_file = true;
              break;
            }
            if(style_file_search_path == style_file_search_path.root_directory())
              break;
            style_file_search_path = style_file_search_path.parent_path();
          }
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
        params = R"("textDocument":{"uri":")" + uri_escaped + R"("},"range":{"start":{"line":)" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(end.get_line()) + ",\"character\":" + std::to_string(end.get_line_offset()) + "}},\"options\":{" + options + "}";
      }
      else {
        method = "textDocument/formatting";
        params = R"("textDocument":{"uri":")" + uri_escaped + R"("},"options":{)" + options + "}";
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

      client->write_request(this, method, R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "context": {"includeDeclaration": true})", [this, &locations, &result_processed](const boost::property_tree::ptree &result, bool error) {
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

  get_token_spelling = [this] {
    auto spelling = get_token(get_buffer()->get_insert()->get_iter());
    if(spelling.empty())
      Info::get().print("No valid symbol found at current cursor location");
    return spelling;
  };

  if(capabilities.rename || capabilities.document_highlight) {
    rename_similar_tokens = [this](const std::string &text) {
      struct Changes {
        std::string file;
        std::vector<LanguageProtocol::TextEdit> text_edits;
      };

      auto previous_text = get_token(get_buffer()->get_insert()->get_iter());
      if(previous_text.empty())
        return;

      auto iter = get_buffer()->get_insert()->get_iter();
      std::vector<Changes> changes_vec;
      std::promise<void> result_processed;
      if(capabilities.rename) {
        client->write_request(this, "textDocument/rename", R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "newName": ")" + text + "\"", [this, &changes_vec, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            boost::filesystem::path project_path;
            auto build = Project::Build::create(file_path);
            if(!build->project_path.empty())
              project_path = build->project_path;
            else
              project_path = file_path.parent_path();
            try {
              if(auto changes_pt = result.get_child_optional("changes")) {
                for(auto file_it = changes_pt->begin(); file_it != changes_pt->end(); ++file_it) {
                  auto file = file_it->first;
                  file.erase(0, 7);
                  if(filesystem::file_in_path(file, project_path)) {
                    std::vector<LanguageProtocol::TextEdit> edits;
                    for(auto edit_it = file_it->second.begin(); edit_it != file_it->second.end(); ++edit_it)
                      edits.emplace_back(edit_it->second);
                    changes_vec.emplace_back(Changes{std::move(file), std::move(edits)});
                  }
                }
              }
              else if(auto changes_pt = result.get_child_optional("documentChanges")) {
                for(auto change_it = changes_pt->begin(); change_it != changes_pt->end(); ++change_it) {
                  LanguageProtocol::TextDocumentEdit text_document_edit(change_it->second);
                  if(filesystem::file_in_path(text_document_edit.file, project_path))
                    changes_vec.emplace_back(Changes{std::move(text_document_edit.file), std::move(text_document_edit.edits)});
                }
              }
            }
            catch(...) {
              changes_vec.clear();
            }
          }
          result_processed.set_value();
        });
      }
      else {
        client->write_request(this, "textDocument/documentHighlight", R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "context": {"includeDeclaration": true})", [this, &changes_vec, &text, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            try {
              std::vector<LanguageProtocol::TextEdit> edits;
              for(auto it = result.begin(); it != result.end(); ++it)
                edits.emplace_back(it->second, text);
              changes_vec.emplace_back(Changes{file_path.string(), std::move(edits)});
            }
            catch(...) {
              changes_vec.clear();
            }
          }
          result_processed.set_value();
        });
      }
      result_processed.get_future().get();

      auto current_view = Notebook::get().get_current_view();

      struct ChangesAndView {
        Changes *changes;
        Source::View *view;
        bool close;
      };
      std::vector<ChangesAndView> changes_and_views;

      for(auto &changes : changes_vec) {
        Source::View *view = nullptr;
        for(auto it = views.begin(); it != views.end(); ++it) {
          if((*it)->file_path == changes.file) {
            view = *it;
            break;
          }
        }
        if(!view) {
          if(!Notebook::get().open(changes.file))
            return;
          view = Notebook::get().get_current_view();
          changes_and_views.emplace_back(ChangesAndView{&changes, view, true});
        }
        else
          changes_and_views.emplace_back(ChangesAndView{&changes, view, false});
      }

      if(current_view)
        Notebook::get().open(current_view);

      if(!changes_and_views.empty()) {
        Terminal::get().print("Renamed ");
        Terminal::get().print(previous_text, true);
        Terminal::get().print(" to ");
        Terminal::get().print(text, true);
        Terminal::get().print(" at:\n");
      }

      for(auto &changes_and_view : changes_and_views) {
        auto changes = changes_and_view.changes;
        auto view = changes_and_view.view;
        auto buffer = view->get_buffer();
        buffer->begin_user_action();

        auto end_iter = buffer->end();
        // If entire buffer is replaced
        if(changes->text_edits.size() == 1 &&
           changes->text_edits[0].range.start.line == 0 && changes->text_edits[0].range.start.character == 0 &&
           (changes->text_edits[0].range.end.line > end_iter.get_line() ||
            (changes->text_edits[0].range.end.line == end_iter.get_line() && changes->text_edits[0].range.end.character >= end_iter.get_line_offset()))) {
          view->replace_text(changes->text_edits[0].new_text);

          Terminal::get().print(filesystem::get_short_path(view->file_path).string() + ":1:1\n");
        }
        else {
          struct TerminalOutput {
            std::string prefix;
            std::string new_text;
            std::string postfix;
          };
          std::list<TerminalOutput> terminal_output_list;

          for(auto edit_it = changes->text_edits.rbegin(); edit_it != changes->text_edits.rend(); ++edit_it) {
            auto start_iter = view->get_iter_at_line_pos(edit_it->range.start.line, edit_it->range.start.character);
            auto end_iter = view->get_iter_at_line_pos(edit_it->range.end.line, edit_it->range.end.character);
            if(view != current_view)
              view->get_buffer()->place_cursor(start_iter);
            buffer->erase(start_iter, end_iter);
            start_iter = view->get_iter_at_line_pos(edit_it->range.start.line, edit_it->range.start.character);

            auto start_line_iter = buffer->get_iter_at_line(start_iter.get_line());
            while((*start_line_iter == ' ' || *start_line_iter == '\t') && start_line_iter < start_iter && start_line_iter.forward_char()) {
            }
            auto end_line_iter = start_iter;
            while(!end_line_iter.ends_line() && end_line_iter.forward_char()) {
            }
            terminal_output_list.emplace_front(TerminalOutput{filesystem::get_short_path(view->file_path).string() + ':' + std::to_string(edit_it->range.start.line + 1) + ':' + std::to_string(edit_it->range.start.character + 1) + ": " + buffer->get_text(start_line_iter, start_iter),
                                                              edit_it->new_text,
                                                              buffer->get_text(start_iter, end_line_iter) + '\n'});

            buffer->insert(start_iter, edit_it->new_text);
          }

          for(auto &output : terminal_output_list) {
            Terminal::get().print(output.prefix);
            Terminal::get().print(output.new_text, true);
            Terminal::get().print(output.postfix);
          }
        }

        buffer->end_user_action();
        if(!view->save())
          return;
      }

      for(auto &changes_and_view : changes_and_views) {
        if(changes_and_view.close)
          Notebook::get().close(changes_and_view.view);
        changes_and_view.close = false;
      }
    };
  }

  if(capabilities.document_symbol) {
    get_methods = [this]() {
      std::vector<std::pair<Offset, std::string>> methods;

      std::promise<void> result_processed;
      client->write_request(this, "textDocument/documentSymbol", R"("textDocument":{"uri":")" + uri_escaped + "\"}", [&result_processed, &methods](const boost::property_tree::ptree &result, bool error) {
        if(!error) {
          std::function<void(const boost::property_tree::ptree &ptee, const std::string &container)> parse_result = [&methods, &parse_result](const boost::property_tree::ptree &pt, const std::string &container) {
            for(auto it = pt.begin(); it != pt.end(); ++it) {
              try {
                auto kind = it->second.get<int>("kind");
                if(kind == 6 || kind == 9 || kind == 12) {
                  std::unique_ptr<LanguageProtocol::Range> range;
                  std::string prefix;
                  if(auto location_pt = it->second.get_child_optional("location")) {
                    LanguageProtocol::Location location(*location_pt);
                    range = std::make_unique<LanguageProtocol::Range>(location.range);
                    std::string container = it->second.get<std::string>("containerName", "");
                    if(container == "null")
                      container.clear();
                    if(!container.empty())
                      prefix = container;
                  }
                  else {
                    range = std::make_unique<LanguageProtocol::Range>(it->second.get_child("range"));
                    if(!container.empty())
                      prefix = container;
                  }
                  methods.emplace_back(Offset(range->start.line, range->start.character), (!prefix.empty() ? Glib::Markup::escape_text(prefix) + ':' : "") + std::to_string(range->start.line + 1) + ": " + "<b>" + Glib::Markup::escape_text(it->second.get<std::string>("name")) + "</b>");
                }
                if(auto children = it->second.get_child_optional("children"))
                  parse_result(*children, (!container.empty() ? container + "::" : "") + it->second.get<std::string>("name"));
              }
              catch(...) {
              }
            }
          };
          parse_result(result, "");
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

  get_fix_its = [this]() {
    if(fix_its.empty())
      Info::get().print("No fix-its found in current buffer");
    return fix_its;
  };
}

void Source::LanguageProtocolView::update_diagnostics_async(std::vector<LanguageProtocol::Diagnostic> &&diagnostics) {
  update_diagnostics_async_count++;
  size_t last_count = update_diagnostics_async_count;
  if(capabilities.code_action && !diagnostics.empty()) {
    dispatcher.post([this, diagnostics = std::move(diagnostics), last_count]() mutable {
      if(last_count != update_diagnostics_async_count)
        return;
      std::string range;
      std::string diagnostics_string;
      for(auto &diagnostic : diagnostics) {
        auto start = get_iter_at_line_pos(diagnostic.range.start.line, diagnostic.range.start.character);
        auto end = get_iter_at_line_pos(diagnostic.range.end.line, diagnostic.range.end.character);
        range = "{\"start\":{\"line\": " + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(end.get_line()) + ",\"character\":" + std::to_string(end.get_line_offset()) + "}}";
        if(!diagnostics_string.empty())
          diagnostics_string += ',';
        diagnostics_string += "{\"range\":" + range + ",\"message\":\"" + LanguageProtocol::escape_text(diagnostic.message) + "\"";
        if(diagnostic.severity != 0)
          diagnostics_string += ",\"severity\":" + std::to_string(diagnostic.severity);
        if(!diagnostic.code.empty())
          diagnostics_string += ",\"code\":\"" + diagnostic.code + "\"";
        diagnostics_string += "}";
      }
      if(diagnostics.size() != 1) { // Use diagnostic range if only one diagnostic, otherwise use whole buffer
        auto start = get_buffer()->begin();
        auto end = get_buffer()->end();
        range = "{\"start\":{\"line\": " + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(end.get_line()) + ",\"character\":" + std::to_string(end.get_line_offset()) + "}}";
      }

      auto request = (R"("textDocument":{"uri":")" + uri_escaped + "\"},\"range\":" + range + ",\"context\":{\"diagnostics\":[" + diagnostics_string + "],\"only\":[\"quickfix\"]}");
      thread_pool.push([this, diagnostics = std::move(diagnostics), request = std::move(request), last_count]() mutable {
        if(last_count != update_diagnostics_async_count)
          return;
        std::promise<void> result_processed;
        client->write_request(this, "textDocument/codeAction", request, [this, &result_processed, &diagnostics, last_count](const boost::property_tree::ptree &result, bool error) {
          if(!error && last_count == update_diagnostics_async_count) {
            try {
              for(auto it = result.begin(); it != result.end(); ++it) {
                auto kind = it->second.get<std::string>("kind", "");
                if(kind == "quickfix" || kind.empty()) { // Workaround for typescript-language-server (kind.empty())
                  auto title = it->second.get<std::string>("title");
                  std::vector<LanguageProtocol::Diagnostic> quickfix_diagnostics;
                  if(auto diagnostics_pt = it->second.get_child_optional("diagnostics")) {
                    for(auto it = diagnostics_pt->begin(); it != diagnostics_pt->end(); ++it)
                      quickfix_diagnostics.emplace_back(it->second);
                  }
                  if(auto changes = it->second.get_child_optional("edit.changes")) {
                    for(auto file_it = changes->begin(); file_it != changes->end(); ++file_it) {
                      for(auto edit_it = file_it->second.begin(); edit_it != file_it->second.end(); ++edit_it) {
                        LanguageProtocol::TextEdit edit(edit_it->second);
                        if(!quickfix_diagnostics.empty()) {
                          for(auto &diagnostic : diagnostics) {
                            for(auto &quickfix_diagnostic : quickfix_diagnostics) {
                              if(diagnostic.message == quickfix_diagnostic.message && diagnostic.range == quickfix_diagnostic.range) {
                                auto pair = diagnostic.quickfixes.emplace(title, std::set<Source::FixIt>{});
                                pair.first->second.emplace(
                                    edit.new_text,
                                    filesystem::get_path_from_uri(file_it->first).string(),
                                    std::make_pair<Offset, Offset>(Offset(edit.range.start.line, edit.range.start.character),
                                                                   Offset(edit.range.end.line, edit.range.end.character)));
                                break;
                              }
                            }
                          }
                        }
                        else { // Workaround for language server that does not report quickfix diagnostics
                          for(auto &diagnostic : diagnostics) {
                            if(edit.range.start.line == diagnostic.range.start.line) {
                              auto pair = diagnostic.quickfixes.emplace(title, std::set<Source::FixIt>{});
                              pair.first->second.emplace(
                                  edit.new_text,
                                  filesystem::get_path_from_uri(file_it->first).string(),
                                  std::make_pair<Offset, Offset>(Offset(edit.range.start.line, edit.range.start.character),
                                                                 Offset(edit.range.end.line, edit.range.end.character)));
                              break;
                            }
                          }
                        }
                      }
                    }
                  }
                  else {
                    auto changes_pt = it->second.get_child_optional("edit.documentChanges");
                    if(!changes_pt) { // Workaround for typescript-language-server
                      if(auto arguments_pt = it->second.get_child_optional("arguments")) {
                        if(!arguments_pt->empty())
                          changes_pt = arguments_pt->begin()->second.get_child_optional("documentChanges");
                      }
                    }
                    if(changes_pt) {
                      for(auto change_it = changes_pt->begin(); change_it != changes_pt->end(); ++change_it) {
                        LanguageProtocol::TextDocumentEdit text_document_edit(change_it->second);
                        for(auto &edit : text_document_edit.edits) {
                          if(!quickfix_diagnostics.empty()) {
                            for(auto &diagnostic : diagnostics) {
                              for(auto &quickfix_diagnostic : quickfix_diagnostics) {
                                if(diagnostic.message == quickfix_diagnostic.message && diagnostic.range == quickfix_diagnostic.range) {
                                  auto pair = diagnostic.quickfixes.emplace(title, std::set<Source::FixIt>{});
                                  pair.first->second.emplace(
                                      edit.new_text,
                                      text_document_edit.file,
                                      std::make_pair<Offset, Offset>(Offset(edit.range.start.line, edit.range.start.character),
                                                                     Offset(edit.range.end.line, edit.range.end.character)));
                                  break;
                                }
                              }
                            }
                          }
                          else { // Workaround for language server that does not report quickfix diagnostics
                            for(auto &diagnostic : diagnostics) {
                              if(edit.range.start.line == diagnostic.range.start.line) {
                                auto pair = diagnostic.quickfixes.emplace(title, std::set<Source::FixIt>{});
                                pair.first->second.emplace(
                                    edit.new_text,
                                    text_document_edit.file,
                                    std::make_pair<Offset, Offset>(Offset(edit.range.start.line, edit.range.start.character),
                                                                   Offset(edit.range.end.line, edit.range.end.character)));
                                break;
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            catch(...) {
            }
          }
          result_processed.set_value();
        });
        result_processed.get_future().get();
        dispatcher.post([this, diagnostics = std::move(diagnostics), last_count]() mutable {
          if(last_count == update_diagnostics_async_count) {
            last_diagnostics = diagnostics;
            update_diagnostics(std::move(diagnostics));
          }
        });
      });
    });
  }
  else {
    dispatcher.post([this, diagnostics = std::move(diagnostics), last_count]() mutable {
      if(last_count == update_diagnostics_async_count) {
        last_diagnostics = diagnostics;
        update_diagnostics(std::move(diagnostics));
      }
    });
  }
}

void Source::LanguageProtocolView::update_diagnostics(std::vector<LanguageProtocol::Diagnostic> diagnostics) {
  diagnostic_offsets.clear();
  diagnostic_tooltips.clear();
  fix_its.clear();
  get_buffer()->remove_tag_by_name("def:warning_underline", get_buffer()->begin(), get_buffer()->end());
  get_buffer()->remove_tag_by_name("def:error_underline", get_buffer()->begin(), get_buffer()->end());
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
    if(diagnostic.severity >= 2)
      num_warnings++;
    else {
      num_errors++;
      error = true;
    }
    num_fix_its += diagnostic.quickfixes.size();

    for(auto &quickfix : diagnostic.quickfixes)
      fix_its.insert(fix_its.end(), quickfix.second.begin(), quickfix.second.end());

    add_diagnostic_tooltip(start, end, error, [this, diagnostic = std::move(diagnostic)](Tooltip &tooltip) {
      if(language_id == "python") { // Python might support markdown in the future
        tooltip.insert_with_links_tagged(diagnostic.message);
        return;
      }
      tooltip.insert_markdown(diagnostic.message);

      if(!diagnostic.related_informations.empty()) {
        auto link_tag = tooltip.buffer->get_tag_table()->lookup("link");
        for(size_t i = 0; i < diagnostic.related_informations.size(); ++i) {
          auto link = filesystem::get_relative_path(diagnostic.related_informations[i].location.file, file_path.parent_path()).string();
          link += ':' + std::to_string(diagnostic.related_informations[i].location.range.start.line + 1);
          link += ':' + std::to_string(diagnostic.related_informations[i].location.range.start.character + 1);

          if(i == 0)
            tooltip.buffer->insert_at_cursor("\n\n");
          else
            tooltip.buffer->insert_at_cursor("\n");
          tooltip.insert_markdown(diagnostic.related_informations[i].message);
          tooltip.buffer->insert_at_cursor(": ");
          tooltip.buffer->insert_with_tag(tooltip.buffer->get_insert()->get_iter(), link, link_tag);
        }
      }

      if(!diagnostic.quickfixes.empty()) {
        if(diagnostic.quickfixes.size() == 1)
          tooltip.buffer->insert_at_cursor("\n\nFix-it:");
        else
          tooltip.buffer->insert_at_cursor("\n\nFix-its:");
        for(auto &quickfix : diagnostic.quickfixes) {
          tooltip.buffer->insert_at_cursor("\n");
          tooltip.insert_markdown(quickfix.first);
        }
      }
    });
  }

  for(auto &mark : type_coverage_marks) {
    add_diagnostic_tooltip(mark.first->get_iter(), mark.second->get_iter(), false, [](Tooltip &tooltip) {
      tooltip.buffer->insert_at_cursor(type_coverage_message);
    });
    num_warnings++;
  }

  status_diagnostics = std::make_tuple(num_warnings, num_errors, num_fix_its);
  if(update_status_diagnostics)
    update_status_diagnostics(this);
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
  client->write_request(this, "textDocument/hover", R"("textDocument": {"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + "}", [this, offset, current_request](const boost::property_tree::ptree &result, bool error) {
    if(!error) {
      // hover result structure vary significantly from the different language servers
      struct Content {
        std::string value;
        std::string kind;
      };
      std::list<Content> contents;
      auto contents_pt = result.get_child_optional("contents");
      if(!contents_pt)
        return;
      auto value = contents_pt->get_value<std::string>("");
      if(!value.empty())
        contents.emplace_back(Content{value, "markdown"});
      else {
        auto value_pt = contents_pt->get_optional<std::string>("value");
        if(value_pt) {
          auto kind = contents_pt->get<std::string>("kind", "");
          if(kind.empty())
            kind = contents_pt->get<std::string>("language", "");
          contents.emplace_back(Content{*value_pt, kind});
        }
        else {
          bool first_value = true;
          for(auto it = contents_pt->begin(); it != contents_pt->end(); ++it) {
            auto value = it->second.get<std::string>("value", "");
            if(!value.empty()) {
              auto kind = it->second.get<std::string>("kind", "");
              if(kind.empty())
                kind = it->second.get<std::string>("language", "");
              if(first_value) // Place first value, which most likely is type information, to front (workaround for flow-bin's language server)
                contents.emplace_front(Content{value, kind});
              else
                contents.emplace_back(Content{value, kind});
              first_value = false;
            }
            else {
              value = it->second.get_value<std::string>("");
              if(!value.empty())
                contents.emplace_back(Content{value, "markdown"});
            }
          }
        }
      }
      if(!contents.empty()) {
        dispatcher.post([this, offset, contents = std::move(contents), current_request]() mutable {
          if(current_request != request_count)
            return;
          if(Notebook::get().get_current_view() != this)
            return;
          if(offset >= get_buffer()->get_char_count())
            return;
          type_tooltips.clear();

          auto token_iters = get_token_iters(get_buffer()->get_iter_at_offset(offset));
          type_tooltips.emplace_back(this, token_iters.first, token_iters.second, [this, offset, contents = std::move(contents)](Tooltip &tooltip) mutable {
            bool first = true;
            if(language_id == "python") { // Python might support markdown in the future
              for(auto &content : contents) {
                if(!first)
                  tooltip.buffer->insert_at_cursor("\n\n");
                first = false;
                if(content.kind == "python")
                  tooltip.insert_code(content.value, content.kind);
                else
                  tooltip.insert_docstring(content.value);
              }
            }
            else {
              for(auto &content : contents) {
                if(!first)
                  tooltip.buffer->insert_at_cursor("\n\n");
                first = false;
                if(content.kind == "plaintext" || content.kind.empty())
                  tooltip.insert_with_links_tagged(content.value);
                else if(content.kind == "markdown")
                  tooltip.insert_markdown(content.value);
                else
                  tooltip.insert_code(content.value, content.kind);
                tooltip.remove_trailing_newlines();
              }
            }

#ifdef JUCI_ENABLE_DEBUG
            if(language_id == "rust" && capabilities.definition) {
              if(Debug::LLDB::get().is_stopped()) {
                Glib::ustring value_type = "Value";

                auto token_iters = get_token_iters(get_buffer()->get_iter_at_offset(offset));
                auto offset = get_declaration(token_iters.first);

                auto variable = get_buffer()->get_text(token_iters.first, token_iters.second);
                Glib::ustring debug_value = Debug::LLDB::get().get_value(variable, offset.file_path, offset.line + 1, offset.index + 1);
                if(debug_value.empty()) {
                  debug_value = Debug::LLDB::get().get_return_value(file_path, token_iters.first.get_line() + 1, token_iters.first.get_line_index() + 1);
                  if(!debug_value.empty())
                    value_type = "Return value";
                }
                if(debug_value.empty()) {
                  auto end = token_iters.second;
                  while((end.ends_line() || *end == ' ' || *end == '\t') && end.forward_char()) {
                  }
                  if(*end != '(') {
                    auto iter = token_iters.first;
                    auto start = iter;
                    while(iter.backward_char()) {
                      if(*iter == '.') {
                        while(iter.backward_char() && (*iter == ' ' || *iter == '\t' || iter.ends_line())) {
                        }
                      }
                      if(!is_token_char(*iter))
                        break;
                      start = iter;
                    }
                    if(is_token_char(*start))
                      debug_value = Debug::LLDB::get().get_value(get_buffer()->get_text(start, token_iters.second));
                  }
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
                    tooltip.buffer->insert_at_cursor("\n\n" + value_type + ":\n");
                    tooltip.insert_code(debug_value.substr(pos + 3, debug_value.size() - (pos + 3) - 1));
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
  client->write_request(this, method, R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + R"(}, "context": {"includeDeclaration": true})", [this, current_request](const boost::property_tree::ptree &result, bool error) {
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
  client->write_request(this, "textDocument/definition", R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(line) + ", \"character\": " + std::to_string(offset) + "}", [this, current_request, line, offset](const boost::property_tree::ptree &result, bool error) {
    if(!error && !result.empty()) {
      dispatcher.post([this, current_request, line, offset] {
        if(current_request != request_count || !clickable_tag_applied)
          return;
        get_buffer()->remove_tag(clickable_tag, get_buffer()->begin(), get_buffer()->end());
        auto range = get_token_iters(get_iter_at_line_pos(line, offset));
        get_buffer()->apply_tag(clickable_tag, range.first, range.second);
      });
    }
  });
}

Source::Offset Source::LanguageProtocolView::get_declaration(const Gtk::TextIter &iter) {
  auto offset = std::make_shared<Offset>();
  std::promise<void> result_processed;
  client->write_request(this, "textDocument/definition", R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(iter.get_line()) + ", \"character\": " + std::to_string(iter.get_line_offset()) + "}", [offset, &result_processed](const boost::property_tree::ptree &result, bool error) {
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

void Source::LanguageProtocolView::setup_signals() {
  if(capabilities.text_document_sync == LanguageProtocol::Capabilities::TextDocumentSync::incremental) {
    get_buffer()->signal_insert().connect(
        [this](const Gtk::TextIter &start, const Glib::ustring &text, int bytes) {
          client->write_notification("textDocument/didChange", R"("textDocument":{"uri":")" + uri_escaped + R"(","version":)" + std::to_string(document_version++) + "},\"contentChanges\":[" + R"({"range":{"start":{"line": )" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(}},"text":")" + LanguageProtocol::escape_text(text.raw()) + "\"}" + "]");
        },
        false);

    get_buffer()->signal_erase().connect(
        [this](const Gtk::TextIter &start, const Gtk::TextIter &end) {
          client->write_notification("textDocument/didChange", R"("textDocument":{"uri":")" + uri_escaped + R"(","version":)" + std::to_string(document_version++) + "},\"contentChanges\":[" + R"({"range":{"start":{"line": )" + std::to_string(start.get_line()) + ",\"character\":" + std::to_string(start.get_line_offset()) + R"(},"end":{"line":)" + std::to_string(end.get_line()) + ",\"character\":" + std::to_string(end.get_line_offset()) + R"(}},"text":""})" + "]");
        },
        false);
  }
  else if(capabilities.text_document_sync == LanguageProtocol::Capabilities::TextDocumentSync::full) {
    get_buffer()->signal_changed().connect([this]() {
      client->write_notification("textDocument/didChange", R"("textDocument":{"uri":")" + uri_escaped + R"(","version":)" + std::to_string(document_version++) + "},\"contentChanges\":[" + R"({"text":")" + LanguageProtocol::escape_text(get_buffer()->get_text().raw()) + "\"}" + "]");
    });
  }
}

void Source::LanguageProtocolView::setup_autocomplete() {
  autocomplete = std::make_unique<Autocomplete>(this, interactive_completion, last_keyval, false);

  if(!capabilities.completion)
    return;

  non_interactive_completion = [this] {
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
      return;
    autocomplete->run();
  };

  autocomplete->reparse = [this] {
    autocomplete_rows.clear();
  };

  if(capabilities.signature_help) {
    // Activate argument completions
    get_buffer()->signal_changed().connect(
        [this] {
          if(!interactive_completion)
            return;
          if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
            return;
          if(!has_focus())
            return;
          if(autocomplete_show_arguments)
            autocomplete->stop();
          autocomplete_show_arguments = false;
          autocomplete_delayed_show_arguments_connection.disconnect();
          autocomplete_delayed_show_arguments_connection = Glib::signal_timeout().connect(
              [this]() {
                if(get_buffer()->get_has_selection())
                  return false;
                if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
                  return false;
                if(!has_focus())
                  return false;
                if(is_possible_argument()) {
                  autocomplete->stop();
                  autocomplete->run();
                }
                return false;
              },
              500);
        },
        false);

    // Remove argument completions
    signal_key_press_event().connect(
        [this](GdkEventKey *event) {
          if(autocomplete_show_arguments && CompletionDialog::get() && CompletionDialog::get()->is_visible() &&
             event->keyval != GDK_KEY_Down && event->keyval != GDK_KEY_Up &&
             event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter &&
             event->keyval != GDK_KEY_ISO_Left_Tab && event->keyval != GDK_KEY_Tab &&
             (event->keyval < GDK_KEY_Shift_L || event->keyval > GDK_KEY_Hyper_R)) {
            get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());
            CompletionDialog::get()->hide();
          }
          return false;
        },
        false);
  }

  autocomplete->is_restart_key = [this](guint keyval) {
    auto iter = get_buffer()->get_insert()->get_iter();
    iter.backward_chars(2);
    if(keyval == '.' || (keyval == ':' && *iter == ':'))
      return true;
    return false;
  };

  std::function<bool(Gtk::TextIter)> is_possible_xml_attribute = [](Gtk::TextIter) { return false; };
  if(is_js) {
    autocomplete->is_restart_key = [](guint keyval) {
      if(keyval == '.' || keyval == ' ')
        return true;
      return false;
    };

    is_possible_xml_attribute = [this](Gtk::TextIter iter) {
      return (*iter == ' ' || iter.ends_line() || *iter == '/' || (*iter == '>' && iter.backward_char())) && find_open_symbol_backward(iter, iter, '<', '>');
    };
  }

  autocomplete->run_check = [this, is_possible_xml_attribute]() {
    auto prefix_start = get_buffer()->get_insert()->get_iter();
    auto prefix_end = prefix_start;

    auto prev = prefix_start;
    prev.backward_char();
    if(!is_code_iter(prev))
      return false;

    size_t count = 0;
    while(prefix_start.backward_char() && is_token_char(*prefix_start))
      ++count;

    autocomplete_enable_snippets = false;
    autocomplete_show_arguments = false;

    if(prefix_start != prefix_end && !is_token_char(*prefix_start))
      prefix_start.forward_char();

    prev = prefix_start;
    prev.backward_char();
    auto prevprev = prev;
    if(*prev == '.') {
      auto iter = prev;
      bool starts_with_num = false;
      size_t count = 0;
      while(iter.backward_char() && is_token_char(*iter)) {
        ++count;
        starts_with_num = Glib::Unicode::isdigit(*iter);
      }
      if((count >= 1 || *iter == ')' || *iter == ']' || *iter == '"' || *iter == '\'' || *iter == '?') && !starts_with_num) {
        {
          LockGuard lock(autocomplete->prefix_mutex);
          autocomplete->prefix = get_buffer()->get_text(prefix_start, prefix_end);
        }
        return true;
      }
    }
    else if((prevprev.backward_char() && *prevprev == ':' && *prev == ':')) {
      {
        LockGuard lock(autocomplete->prefix_mutex);
        autocomplete->prefix = get_buffer()->get_text(prefix_start, prefix_end);
      }
      return true;
    }
    else if(count >= 3) { // part of symbol
      {
        LockGuard lock(autocomplete->prefix_mutex);
        autocomplete->prefix = get_buffer()->get_text(prefix_start, prefix_end);
      }
      autocomplete_enable_snippets = true;
      return true;
    }
    if(is_possible_argument()) {
      autocomplete_show_arguments = true;
      LockGuard lock(autocomplete->prefix_mutex);
      autocomplete->prefix = "";
      return true;
    }
    if(is_possible_xml_attribute(prefix_start)) {
      LockGuard lock(autocomplete->prefix_mutex);
      autocomplete->prefix = "";
      return true;
    }
    if(!interactive_completion) {
      {
        LockGuard lock(autocomplete->prefix_mutex);
        autocomplete->prefix = get_buffer()->get_text(prefix_start, prefix_end);
      }
      auto prevprev = prev;
      autocomplete_enable_snippets = !(*prev == '.' || (prevprev.backward_char() && *prevprev == ':' && *prev == ':'));
      return true;
    }

    return false;
  };

  autocomplete->before_add_rows = [this] {
    status_state = "autocomplete...";
    if(update_status_state)
      update_status_state(this);
  };

  autocomplete->after_add_rows = [this] {
    status_state = "";
    if(update_status_state)
      update_status_state(this);
  };

  autocomplete->add_rows = [this](std::string &buffer, int line_number, int column) {
    if(autocomplete->state == Autocomplete::State::starting) {
      autocomplete_rows.clear();
      std::promise<void> result_processed;
      if(autocomplete_show_arguments) {
        if(!capabilities.signature_help)
          return true;
        dispatcher.post([this, line_number, column, &result_processed] {
          // Find current parameter number and previously used named parameters
          unsigned current_parameter_position = 0;
          auto named_parameter_symbol = get_named_parameter_symbol();
          std::set<std::string> used_named_parameters;
          auto iter = get_buffer()->get_insert()->get_iter();
          int para_count = 0;
          int square_count = 0;
          int angle_count = 0;
          int curly_count = 0;
          while(iter.backward_char() && backward_to_code(iter)) {
            if(para_count == 0 && square_count == 0 && angle_count == 0 && curly_count == 0) {
              if(named_parameter_symbol && (*iter == ',' || *iter == '(')) {
                auto next = iter;
                if(next.forward_char() && forward_to_code(next)) {
                  auto pair = get_token_iters(next);
                  if(pair.first != pair.second) {
                    auto symbol = pair.second;
                    if(forward_to_code(symbol) && *symbol == static_cast<unsigned char>(*named_parameter_symbol))
                      used_named_parameters.emplace(get_buffer()->get_text(pair.first, pair.second));
                  }
                }
              }
              if(*iter == ',')
                ++current_parameter_position;
              else if(*iter == '(')
                break;
            }
            if(*iter == '(')
              ++para_count;
            else if(*iter == ')')
              --para_count;
            else if(*iter == '[')
              ++square_count;
            else if(*iter == ']')
              --square_count;
            else if(*iter == '<')
              ++angle_count;
            else if(*iter == '>')
              --angle_count;
            else if(*iter == '{')
              ++curly_count;
            else if(*iter == '}')
              --curly_count;
          }
          bool using_named_parameters = named_parameter_symbol && !(current_parameter_position > 0 && used_named_parameters.empty());

          client->write_request(this, "textDocument/signatureHelp", R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(line_number - 1) + ", \"character\": " + std::to_string(column - 1) + "}", [this, &result_processed, current_parameter_position, using_named_parameters, used_named_parameters = std::move(used_named_parameters)](const boost::property_tree::ptree &result, bool error) {
            if(!error) {
              auto signatures = result.get_child("signatures", boost::property_tree::ptree());
              for(auto signature_it = signatures.begin(); signature_it != signatures.end(); ++signature_it) {
                auto parameters = signature_it->second.get_child("parameters", boost::property_tree::ptree());
                unsigned parameter_position = 0;
                for(auto parameter_it = parameters.begin(); parameter_it != parameters.end(); ++parameter_it) {
                  if(parameter_position == current_parameter_position || using_named_parameters) {
                    auto label = parameter_it->second.get<std::string>("label", "");
                    auto insert = label;
                    auto documentation = parameter_it->second.get<std::string>("documentation", "");
                    std::string kind;
                    if(documentation.empty()) {
                      auto documentation_pt = parameter_it->second.get_child_optional("documentation");
                      if(documentation_pt) {
                        documentation = documentation_pt->get<std::string>("value", "");
                        kind = documentation_pt->get<std::string>("kind", "");
                      }
                    }
                    if(documentation == "null") // Python erroneously returns "null" when a parameter is not documented
                      documentation.clear();
                    if(!using_named_parameters || used_named_parameters.find(insert) == used_named_parameters.end()) {
                      autocomplete->rows.emplace_back(std::move(label));
                      autocomplete_rows.emplace_back(AutocompleteRow{std::move(insert), {}, std::move(documentation), std::move(kind)});
                    }
                  }
                  parameter_position++;
                }
              }
            }
            result_processed.set_value();
          });
        });
      }
      else {
        client->write_request(this, "textDocument/completion", R"("textDocument":{"uri":")" + uri_escaped + R"("}, "position": {"line": )" + std::to_string(line_number - 1) + ", \"character\": " + std::to_string(column - 1) + "}", [this, &result_processed](const boost::property_tree::ptree &result, bool error) {
          if(!error) {
            boost::property_tree::ptree::const_iterator begin, end;
            if(auto items = result.get_child_optional("items")) {
              begin = items->begin();
              end = items->end();
            }
            else {
              begin = result.begin();
              end = result.end();
            }
            std::string prefix;
            {
              LockGuard lock(autocomplete->prefix_mutex);
              prefix = autocomplete->prefix;
            }
            for(auto it = begin; it != end; ++it) {
              auto label = it->second.get<std::string>("label", "");
              if(starts_with(label, prefix)) {
                auto detail = it->second.get<std::string>("detail", "");
                auto documentation = it->second.get<std::string>("documentation", "");
                std::string documentation_kind;
                if(documentation.empty()) {
                  if(auto documentation_pt = it->second.get_child_optional("documentation")) {
                    documentation = documentation_pt->get<std::string>("value", "");
                    documentation_kind = documentation_pt->get<std::string>("kind", "");
                  }
                }
                auto insert = it->second.get<std::string>("insertText", "");
                if(insert.empty())
                  insert = it->second.get<std::string>("textEdit.newText", "");
                if(insert.empty())
                  insert = label;
                if(!insert.empty()) {
                  auto kind = it->second.get<int>("kind", 0);
                  if(kind >= 2 && kind <= 4 && insert.find('(') == std::string::npos) // If kind is method, function or constructor, but parentheses are missing
                    insert += "(${1:})";
                  autocomplete->rows.emplace_back(std::move(label));
                  autocomplete_rows.emplace_back(AutocompleteRow{std::move(insert), std::move(detail), std::move(documentation), std::move(documentation_kind)});
                }
              }
            }

            if(autocomplete_enable_snippets) {
              LockGuard lock(snippets_mutex);
              if(snippets) {
                for(auto &snippet : *snippets) {
                  if(starts_with(snippet.prefix, prefix)) {
                    autocomplete->rows.emplace_back(snippet.prefix);
                    autocomplete_rows.emplace_back(AutocompleteRow{snippet.body, {}, snippet.description, {}});
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
    return true;
  };

  autocomplete->on_show = [this] {
    hide_tooltips();
  };

  autocomplete->on_hide = [this] {
    autocomplete_rows.clear();
  };

  autocomplete->on_select = [this](unsigned int index, const std::string &text, bool hide_window) {
    auto insert = hide_window ? autocomplete_rows[index].insert : text;

    get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());

    // Do not insert function/template parameters if they already exist
    {
      auto iter = get_buffer()->get_insert()->get_iter();
      if(*iter == '(' || *iter == '<') {
        auto bracket_pos = insert.find(*iter);
        if(bracket_pos != std::string::npos)
          insert.erase(bracket_pos);
      }
    }

    // Do not instert ?. after ., instead replace . with ?.
    if(1 < insert.size() && insert[0] == '?' && insert[1] == '.') {
      auto iter = get_buffer()->get_insert()->get_iter();
      auto prev = iter;
      if(prev.backward_char() && *prev == '.') {
        get_buffer()->erase(prev, iter);
      }
    }

    if(hide_window) {
      if(autocomplete_show_arguments) {
        if(auto symbol = get_named_parameter_symbol()) // Do not select named parameters in for instance Python
          get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert + *symbol);
        else {
          get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert);
          int start_offset = CompletionDialog::get()->start_mark->get_iter().get_offset();
          int end_offset = CompletionDialog::get()->start_mark->get_iter().get_offset() + insert.size();
          get_buffer()->select_range(get_buffer()->get_iter_at_offset(start_offset), get_buffer()->get_iter_at_offset(end_offset));
        }
        return;
      }

      insert_snippet(CompletionDialog::get()->start_mark->get_iter(), insert);
      auto iter = get_buffer()->get_insert()->get_iter();
      if(*iter == ')' && iter.backward_char() && *iter == '(') { // If no arguments, try signatureHelp
        last_keyval = '(';
        autocomplete->run();
      }
    }
    else
      get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), insert);
  };

  autocomplete->set_tooltip_buffer = [this](unsigned int index) -> std::function<void(Tooltip & tooltip)> {
    auto autocomplete = autocomplete_rows[index];
    if(autocomplete.detail.empty() && autocomplete.documentation.empty())
      return nullptr;
    return [this, autocomplete = std::move(autocomplete)](Tooltip &tooltip) mutable {
      if(language_id == "python") // Python might support markdown in the future
        tooltip.insert_docstring(autocomplete.documentation);
      else {
        if(!autocomplete.detail.empty()) {
          tooltip.insert_code(autocomplete.detail, language);
          tooltip.remove_trailing_newlines();
        }
        if(!autocomplete.documentation.empty()) {
          if(tooltip.buffer->size() > 0)
            tooltip.buffer->insert_at_cursor("\n\n");
          if(autocomplete.kind == "plaintext" || autocomplete.kind.empty())
            tooltip.insert_with_links_tagged(autocomplete.documentation);
          else if(autocomplete.kind == "markdown")
            tooltip.insert_markdown(autocomplete.documentation);
          else
            tooltip.insert_code(autocomplete.documentation, autocomplete.kind);
        }
      }
    };
  };
}

boost::optional<char> Source::LanguageProtocolView::get_named_parameter_symbol() {
  if(language_id == "python") // TODO: add more languages that supports named parameters
    return '=';
  return {};
}

void Source::LanguageProtocolView::update_type_coverage() {
  if(capabilities.type_coverage) {
    client->write_request(this, "textDocument/typeCoverage", R"("textDocument": {"uri":")" + uri_escaped + "\"}", [this](const boost::property_tree::ptree &result, bool error) {
      if(error) {
        if(update_type_coverage_retries > 0) { // Retry typeCoverage request, since these requests can fail while waiting for language server to start
          dispatcher.post([this] {
            update_type_coverage_connection.disconnect();
            update_type_coverage_connection = Glib::signal_timeout().connect(
                [this]() {
                  --update_type_coverage_retries;
                  update_type_coverage();
                  return false;
                },
                1000);
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
        type_coverage_marks.clear();
        for(auto &range : ranges) {
          auto start = get_iter_at_line_pos(range.start.line, range.start.character);
          auto end = get_iter_at_line_pos(range.end.line, range.end.character);
          type_coverage_marks.emplace_back(start, end);
        }

        update_diagnostics(last_diagnostics);
      });
    });
  }
}
