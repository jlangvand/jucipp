#include "source_clang.h"
#include "config.h"
#include "project_build.h"
#include "terminal.h"
#ifdef JUCI_ENABLE_DEBUG
#include "debug_lldb.h"
#endif
#include "compile_commands.h"
#include "ctags.h"
#include "dialogs.h"
#include "documentation_cppreference.h"
#include "filesystem.h"
#include "info.h"
#include "selection_dialog.h"
#include "usages_clang.h"

const std::regex include_regex(R"(^[ \t]*#[ \t]*include[ \t]*[<"]([^<>"]+)[>"].*$)");

Source::ClangViewParse::ClangViewParse(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language)
    : BaseView(file_path, language), Source::View(file_path, language) {
  Usages::Clang::erase_cache(file_path);

  auto tag_table = get_buffer()->get_tag_table();
  for(auto &item : clang_types()) {
    auto tag = tag_table->lookup(item.second);
    if(!tag)
      syntax_tags.emplace(item.first, get_buffer()->create_tag(item.second));
    else
      syntax_tags.emplace(item.first, tag);
  }

  if(get_buffer()->size() == 0 && (language->get_id() == "chdr" || language->get_id() == "cpphdr")) {
    disable_spellcheck = true;
    get_buffer()->insert_at_cursor("#pragma once\n");
    disable_spellcheck = false;
    Info::get().print("Added \"#pragma once\" to empty C/C++ header file");
  }

  parse_initialize();

  get_buffer()->signal_changed().connect([this]() {
    soft_reparse(true);
  });
}

void Source::ClangViewParse::rename(const boost::filesystem::path &path) {
  Source::DiffView::rename(path);
  full_reparse();
}

bool Source::ClangViewParse::save() {
  if(!Source::View::save())
    return false;

  if(language->get_id() == "chdr" || language->get_id() == "cpphdr") {
    for(auto &view : views) {
      if(auto clang_view = dynamic_cast<Source::ClangView *>(view)) {
        if(this != clang_view)
          clang_view->soft_reparse_needed = true;
      }
    }
  }
  return true;
}

void Source::ClangViewParse::configure() {
  Source::View::configure();

  auto scheme = get_source_buffer()->get_style_scheme();
  auto tag_table = get_buffer()->get_tag_table();
  for(auto &item : clang_types()) {
    auto tag = get_buffer()->get_tag_table()->lookup(item.second);
    if(tag) {
      auto style = scheme->get_style(item.second);
      if(style) {
        if(style->property_foreground_set())
          tag->property_foreground() = style->property_foreground();
        if(style->property_background_set())
          tag->property_background() = style->property_background();
        if(style->property_strikethrough_set())
          tag->property_strikethrough() = style->property_strikethrough();
        //   //    if (style->property_bold_set()) tag->property_weight() = style->property_bold();
        //   //    if (style->property_italic_set()) tag->property_italic() = style->property_italic();
        //   //    if (style->property_line_background_set()) tag->property_line_background() = style->property_line_background();
        //   // if (style->property_underline_set()) tag->property_underline() = style->property_underline();
      }
    }
  }
}

void Source::ClangViewParse::parse_initialize() {
  hide_tooltips();
  parsed = false;
  if(parse_thread.joinable())
    parse_thread.join();
  parse_state = ParseState::processing;
  parse_process_state = ParseProcessState::starting;

  auto buffer_ = get_buffer()->get_text();
  auto &buffer_raw = const_cast<std::string &>(buffer_.raw());

  if(!Config::get().log.libclang) {
    // Remove includes for first parse for initial syntax highlighting
    std::size_t pos = 0;
    while((pos = buffer_raw.find("#include", pos)) != std::string::npos) {
      auto start_pos = pos;
      pos = buffer_raw.find('\n', pos + 8);
      if(pos == std::string::npos)
        break;
      if(start_pos == 0 || buffer_raw[start_pos - 1] == '\n') {
        buffer_raw.replace(start_pos, pos - start_pos, pos - start_pos, ' ');
      }
      pos++;
    }
  }

  if(language && (language->get_id() == "chdr" || language->get_id() == "cpphdr"))
    clangmm::remove_include_guard(buffer_raw);

  auto build = Project::Build::create(file_path);
  if(build->project_path.empty())
    Info::get().print(file_path.filename().string() + ": could not find a supported build system");
  build->update_default();
  auto arguments = CompileCommands::get_arguments(build->get_default_path(), file_path);
  clang_tu = std::make_unique<clangmm::TranslationUnit>(std::make_shared<clangmm::Index>(0, Config::get().log.libclang), file_path.string(), arguments, &buffer_raw);
  clang_tokens = clang_tu->get_tokens();
  clang_tokens_offsets.clear();
  clang_tokens_offsets.reserve(clang_tokens->size());
  for(auto &token : *clang_tokens)
    clang_tokens_offsets.emplace_back(token.get_source_range().get_offsets());
  {
    LockGuard lock(parse_mutex);
    update_syntax();
  }

  status_state = "parsing...";
  if(update_status_state)
    update_status_state(this);
  parse_thread = std::thread([this]() {
    while(true) {
      while(parse_state == ParseState::processing && parse_process_state != ParseProcessState::starting && parse_process_state != ParseProcessState::processing)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if(parse_state != ParseState::processing)
        break;
      auto expected = ParseProcessState::starting;
      if(parse_process_state.compare_exchange_strong(expected, ParseProcessState::preprocessing)) {
        dispatcher.post([this] {
          auto expected = ParseProcessState::preprocessing;
          if(parse_mutex.try_lock()) {
            if(parse_process_state.compare_exchange_strong(expected, ParseProcessState::processing))
              parse_thread_buffer = get_buffer()->get_text();
            parse_mutex.unlock();
          }
          else
            parse_process_state.compare_exchange_strong(expected, ParseProcessState::starting);
        });
      }
      else if(parse_process_state == ParseProcessState::processing && parse_mutex.try_lock()) {
        auto &parse_thread_buffer_raw = const_cast<std::string &>(parse_thread_buffer.raw());
        if(this->language && (this->language->get_id() == "chdr" || this->language->get_id() == "cpphdr"))
          clangmm::remove_include_guard(parse_thread_buffer_raw);
        auto status = clang_tu->reparse(parse_thread_buffer_raw);
        if(status == 0) {
          auto expected = ParseProcessState::processing;
          if(parse_process_state.compare_exchange_strong(expected, ParseProcessState::postprocessing)) {
            clang_tokens = clang_tu->get_tokens();
            clang_tokens_offsets.clear();
            clang_tokens_offsets.reserve(clang_tokens->size());
            for(auto &token : *clang_tokens)
              clang_tokens_offsets.emplace_back(token.get_source_range().get_offsets());
            clang_diagnostics = clang_tu->get_diagnostics();
            parse_mutex.unlock();
            dispatcher.post([this] {
              if(parse_mutex.try_lock()) {
                auto expected = ParseProcessState::postprocessing;
                if(parse_process_state.compare_exchange_strong(expected, ParseProcessState::idle)) {
                  update_syntax();
                  update_diagnostics();
                  parsed = true;
                  status_state = "";
                  if(update_status_state)
                    update_status_state(this);
                }
                parse_mutex.unlock();
              }
            });
          }
          else
            parse_mutex.unlock();
        }
        else {
          parse_state = ParseState::stop;
          parse_mutex.unlock();
          dispatcher.post([this] {
            Terminal::get().print("Error: failed to reparse " + this->file_path.string() + ".\n", true);
            status_state = "";
            if(update_status_state)
              update_status_state(this);
            status_diagnostics = std::make_tuple(0, 0, 0);
            if(update_status_diagnostics)
              update_status_diagnostics(this);
          });
        }
      }
    }
  });
}

void Source::ClangViewParse::soft_reparse(bool delayed) {
  soft_reparse_needed = false;
  parsed = false;
  delayed_reparse_connection.disconnect();

  if(parse_state != ParseState::processing)
    return;

  parse_process_state = ParseProcessState::idle;

  auto reparse = [this] {
    parsed = false;
    auto expected = ParseProcessState::idle;
    if(parse_process_state.compare_exchange_strong(expected, ParseProcessState::starting)) {
      status_state = "parsing...";
      if(update_status_state)
        update_status_state(this);
    }
    return false;
  };
  if(delayed)
    delayed_reparse_connection = Glib::signal_timeout().connect(reparse, 1000);
  else
    reparse();
}

const std::map<int, std::string> &Source::ClangViewParse::clang_types() {
  static std::map<int, std::string> types{
      {8, "def:function"},
      {21, "def:function"},
      {22, "def:identifier"},
      {24, "def:function"},
      {25, "def:function"},
      {43, "def:type"},
      {44, "def:type"},
      {45, "def:type"},
      {46, "def:identifier"},
      {109, "def:string"},
      {702, "def:statement"},
      {705, "def:comment"}
  };
  return types;
}

void Source::ClangViewParse::update_syntax() {
  auto buffer = get_buffer();
  const auto apply_tag = [this, buffer](const std::pair<clangmm::Offset, clangmm::Offset> &offsets, int type) {
    auto syntax_tag_it = syntax_tags.find(type);
    if(syntax_tag_it != syntax_tags.end()) {
      Gtk::TextIter begin_iter = buffer->get_iter_at_line_index(offsets.first.line - 1, offsets.first.index - 1);
      Gtk::TextIter end_iter = buffer->get_iter_at_line_index(offsets.second.line - 1, offsets.second.index - 1);
      buffer->apply_tag(syntax_tag_it->second, begin_iter, end_iter);
    }
  };

  for(auto &pair : syntax_tags)
    buffer->remove_tag(pair.second, buffer->begin(), buffer->end());

  for(size_t c = 0; c < clang_tokens->size(); ++c) {
    auto &token = (*clang_tokens)[c];
    auto &token_offsets = clang_tokens_offsets[c];
    //if(token.get_kind()==clangmm::Token::Kind::Token_Punctuation)
    //ranges.emplace_back(token_offset, static_cast<int>(token.get_cursor().get_kind()));
    auto token_kind = token.get_kind();
    if(token_kind == clangmm::Token::Kind::Keyword)
      apply_tag(token_offsets, 702);
    else if(token_kind == clangmm::Token::Kind::Identifier) {
      auto cursor_kind = token.get_cursor().get_kind();
      if(cursor_kind == clangmm::Cursor::Kind::DeclRefExpr || cursor_kind == clangmm::Cursor::Kind::MemberRefExpr)
        cursor_kind = token.get_cursor().get_referenced().get_kind();
      if(cursor_kind != clangmm::Cursor::Kind::PreprocessingDirective)
        apply_tag(token_offsets, static_cast<int>(cursor_kind));
    }
    else if(token_kind == clangmm::Token::Kind::Literal)
      apply_tag(token_offsets, static_cast<int>(clangmm::Cursor::Kind::StringLiteral));
    else if(token_kind == clangmm::Token::Kind::Comment)
      apply_tag(token_offsets, 705);
  }
}

void Source::ClangViewParse::update_diagnostics() {
  clear_diagnostic_tooltips();
  fix_its.clear();
  size_t num_warnings = 0;
  size_t num_errors = 0;
  size_t num_fix_its = 0;
  for(auto &diagnostic : clang_diagnostics) {
    if(diagnostic.path == file_path.string()) {
      int line = diagnostic.offsets.first.line - 1;
      if(line < 0 || line >= get_buffer()->get_line_count())
        line = get_buffer()->get_line_count() - 1;
      auto start = get_iter_at_line_end(line);
      int index = diagnostic.offsets.first.index - 1;
      if(index >= 0 && index < start.get_line_index())
        start = get_buffer()->get_iter_at_line_index(line, index);
      if(start.ends_line()) {
        while(!start.is_start() && start.ends_line())
          start.backward_char();
      }
      diagnostic_offsets.emplace(start.get_offset());

      line = diagnostic.offsets.second.line - 1;
      if(line < 0 || line >= get_buffer()->get_line_count())
        line = get_buffer()->get_line_count() - 1;
      auto end = get_iter_at_line_end(line);
      index = diagnostic.offsets.second.index - 1;
      if(index >= 0 && index < end.get_line_index())
        end = get_buffer()->get_iter_at_line_index(line, index);

      bool error = false;
      if(diagnostic.severity <= clangmm::Diagnostic::Severity::Warning)
        num_warnings++;
      else {
        num_errors++;
        error = true;
      }

      std::string fix_its_string;
      unsigned fix_its_count = 0;
      for(auto &fix_it : diagnostic.fix_its) {
        auto clang_offsets = fix_it.offsets;
        std::pair<Offset, Offset> offsets;
        offsets.first.line = clang_offsets.first.line - 1;
        offsets.first.index = clang_offsets.first.index - 1;
        offsets.second.line = clang_offsets.second.line - 1;
        offsets.second.index = clang_offsets.second.index - 1;

        fix_its.emplace_back(fix_it.source, offsets);

        if(fix_its_string.size() > 0)
          fix_its_string += '\n';
        fix_its_string += fix_its.back().string(get_buffer());
        fix_its_count++;
        num_fix_its++;
      }

      if(fix_its_count == 1)
        fix_its_string.insert(0, "Fix-it:\n");
      else if(fix_its_count > 1)
        fix_its_string.insert(0, "Fix-its:\n");

      if(!fix_its_string.empty())
        diagnostic.spelling += "\n\n" + fix_its_string;

      add_diagnostic_tooltip(start, end, error, [spelling = std::move(diagnostic.spelling)](Tooltip &tooltip) {
        tooltip.buffer->insert_at_cursor(spelling);
      });
    }
  }
  status_diagnostics = std::make_tuple(num_warnings, num_errors, num_fix_its);
  if(update_status_diagnostics)
    update_status_diagnostics(this);
}

void Source::ClangViewParse::show_type_tooltips(const Gdk::Rectangle &rectangle) {
  if(parsed) {
    Gtk::TextIter iter;
    int location_x, location_y;
    window_to_buffer_coords(Gtk::TextWindowType::TEXT_WINDOW_TEXT, rectangle.get_x(), rectangle.get_y(), location_x, location_y);
    location_x += (rectangle.get_width() - 1) / 2;
    get_iter_at_location(iter, location_x, location_y);
    Gdk::Rectangle iter_rectangle;
    get_iter_location(iter, iter_rectangle);
    if(iter.ends_line() && location_x > iter_rectangle.get_x())
      return;

    auto line = static_cast<unsigned>(iter.get_line());
    auto index = static_cast<unsigned>(iter.get_line_index());
    type_tooltips.clear();
    for(size_t c = clang_tokens->size() - 1; c != static_cast<size_t>(-1); --c) {
      auto &token = (*clang_tokens)[c];
      auto &token_offsets = clang_tokens_offsets[c];
      auto token_spelling = token.get_spelling();
      if(token.is_identifier() || token_spelling == "auto" || token_spelling == "[" || token_spelling == "]" || token_spelling == "*" || token_spelling == "&") {
        if(line == token_offsets.first.line - 1 && index >= token_offsets.first.index - 1 && index <= token_offsets.second.index - 1) {
          auto cursor = token.get_cursor();
          auto referenced = cursor.get_referenced();
          if(referenced || token_spelling == "[" || token_spelling == "]" || token_spelling == "*" || token_spelling == "&") {
            auto start = get_buffer()->get_iter_at_line_index(token_offsets.first.line - 1, token_offsets.first.index - 1);
            auto end = get_buffer()->get_iter_at_line_index(token_offsets.second.line - 1, token_offsets.second.index - 1);

            type_tooltips.emplace_back(this, get_buffer()->create_mark(start), get_buffer()->create_mark(end), [this, token](Tooltip &tooltip) {
              auto cursor = token.get_cursor();
              tooltip.buffer->insert(tooltip.buffer->get_insert()->get_iter(), "Type: " + cursor.get_type_description());
              auto brief_comment = cursor.get_brief_comments();
              if(brief_comment != "")
                tooltip.insert_with_links_tagged("\n\n" + brief_comment);

#ifdef JUCI_ENABLE_DEBUG
              if(Debug::LLDB::get().is_stopped()) {
                Glib::ustring value_type = "Value";
                Glib::ustring debug_value;
                auto referenced = cursor.get_referenced();
                auto kind = clangmm::Cursor::Kind::UnexposedDecl;
                if(referenced) {
                  kind = referenced.get_kind();
                  auto location = referenced.get_source_location();
                  debug_value = Debug::LLDB::get().get_value(token.get_spelling(), location.get_path(), location.get_offset().line, location.get_offset().index);
                }
                if(debug_value.empty()) {
                  // Attempt to get value from expression (for instance: (*a).b.c, or: (*d)[1 + 1])
                  auto is_safe = [](const clangmm::Cursor &cursor) {
                    auto referenced = cursor.get_referenced();
                    if(referenced) {
                      auto kind = referenced.get_kind();
                      // operator[] is passed even without being const for convenience purposes
                      if(!clang_CXXMethod_isConst(referenced.cx_cursor) && referenced.get_spelling() != "operator[]" &&
                         (kind == clangmm::Cursor::Kind::CXXMethod || kind == clangmm::Cursor::Kind::FunctionDecl ||
                          kind == clangmm::Cursor::Kind::Constructor || kind == clangmm::Cursor::Kind::Destructor ||
                          kind == clangmm::Cursor::Kind::FunctionTemplate || kind == clangmm::Cursor::Kind::ConversionFunction)) {
                        return false;
                      }
                    }
                    return true;
                  };

                  // Do not call state altering expressions:
                  if(is_safe(cursor)) {
                    auto offsets = cursor.get_source_range().get_offsets();
                    auto start = get_buffer()->get_iter_at_line_index(offsets.first.line - 1, offsets.first.index - 1);
                    auto end = get_buffer()->get_iter_at_line_index(offsets.second.line - 1, offsets.second.index - 1);

                    std::string expression;
                    // Get full expression from cursor parent:
                    if(*start == '[' || (kind == clangmm::Cursor::Kind::CXXMethod && (*start == '<' || *start == '>' || *start == '=' || *start == '!' ||
                                                                                      *start == '+' || *start == '-' || *start == '*' || *start == '/' ||
                                                                                      *start == '%' || *start == '&' || *start == '|' || *start == '^' ||
                                                                                      *end == '('))) {
                      struct VisitorData {
                        std::pair<clangmm::Offset, clangmm::Offset> cursor_offsets;
                        clangmm::Cursor parent;
                      };
                      VisitorData visitor_data{cursor.get_source_range().get_offsets(), {}};
                      auto start_cursor = cursor;
                      for(auto parent = cursor.get_semantic_parent(); parent.get_kind() != clangmm::Cursor::Kind::TranslationUnit &&
                                                                      parent.get_kind() != clangmm::Cursor::Kind::ClassDecl; parent = parent.get_semantic_parent())
                        start_cursor = parent;
                      clang_visitChildren(start_cursor.cx_cursor, [](CXCursor cx_cursor, CXCursor cx_parent, CXClientData data_) {
                        auto data = static_cast<VisitorData *>(data_);
                        auto offsets = clangmm::Cursor(cx_cursor).get_source_range().get_offsets();
                        if(offsets == data->cursor_offsets) {
                          data->parent = clangmm::Cursor(cx_parent);
                          return CXChildVisit_Break;
                        }
                        return CXChildVisit_Recurse;
                      }, &visitor_data);
                      if(visitor_data.parent)
                        cursor = visitor_data.parent;
                    }

                    // Check children
                    std::vector<clangmm::Cursor> children;
                    clang_visitChildren(cursor.cx_cursor, [](CXCursor cx_cursor, CXCursor /*parent*/, CXClientData data) {
                      static_cast<std::vector<clangmm::Cursor> *>(data)->emplace_back(cx_cursor);
                      return CXChildVisit_Continue;
                    }, &children);

                    // Check if expression can be called without altering state
                    bool call_expression = true;
                    for(auto &child : children) {
                      if(!is_safe(child)) {
                        call_expression = false;
                        break;
                      }
                    }

                    if(call_expression) {
                      offsets = cursor.get_source_range().get_offsets();
                      start = get_iter_at_line_index(offsets.first.line - 1, offsets.first.index - 1);
                      end = get_iter_at_line_index(offsets.second.line - 1, offsets.second.index - 1);

                      expression = get_buffer()->get_text(start, end).raw();

                      if(!expression.empty()) {
                        // Check for C-like assignment/increment/decrement (non-const) operators
                        char last_last_chr = 0;
                        char last_chr = expression[0];
                        for(size_t i = 1; i < expression.size(); ++i) {
                          auto &chr = expression[i];
                          if((last_chr == '+' && (chr == '+' || chr == '=')) ||
                             (last_chr == '-' && (chr == '-' || chr == '=')) ||
                             (last_chr == '*' && chr == '=') ||
                             (last_chr == '/' && chr == '=') ||
                             (last_chr == '%' && chr == '=') ||
                             (last_chr == '&' && chr == '=') ||
                             (last_chr == '|' && chr == '=') ||
                             (last_chr == '^' && chr == '=') ||
                             // <<= >>=
                             (chr == '=' && ((last_last_chr == '<' && last_chr == '<') || (last_last_chr == '>' && last_chr == '>'))) ||
                             // Checks for = (not ==. .== !=. <=. >=. <=>)
                             (last_chr == '=' && last_last_chr != '=' && chr != '=' && last_last_chr != '!' && last_last_chr != '<' && last_last_chr != '>' &&
                              !(last_last_chr == '<' && chr == '>'))) {
                            call_expression = false;
                            break;
                          }
                          last_last_chr = last_chr;
                          last_chr = chr;
                        }

                        if(call_expression)
                          debug_value = Debug::LLDB::get().get_value(expression);
                      }
                    }
                  }
                }
                if(debug_value.empty()) {
                  value_type = "Return value";
                  auto offsets = token.get_source_range().get_offsets();
                  debug_value = Debug::LLDB::get().get_return_value(token.get_source_location().get_path(), offsets.first.line, offsets.first.index);
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
                    tooltip.buffer->insert(tooltip.buffer->get_insert()->get_iter(), (tooltip.buffer->size() > 0 ? "\n\n" : "") + value_type + ": " + debug_value.substr(pos + 3, debug_value.size() - (pos + 3) - 1));
                  }
                }
              }
#endif
            });
            type_tooltips.show();
            return;
          }
        }
      }
    }
  }
}


Source::ClangViewAutocomplete::ClangViewAutocomplete(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language)
    : BaseView(file_path, language), Source::ClangViewParse(file_path, language), autocomplete(this, interactive_completion, last_keyval, true) {
  non_interactive_completion = [this] {
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
      return;
    autocomplete.run();
  };

  autocomplete.is_processing = [this] {
    return parse_state == ParseState::processing;
  };

  autocomplete.reparse = [this] {
    selected_completion_string = nullptr;
    code_complete_results = nullptr;
    soft_reparse(true);
  };

  autocomplete.cancel_reparse = [this] {
    delayed_reparse_connection.disconnect();
  };

  autocomplete.get_parse_lock = [this]() {
    return std::make_unique<LockGuard>(parse_mutex);
  };

  autocomplete.stop_parse = [this]() {
    parse_process_state = ParseProcessState::idle;
  };

  // Activate argument completions
  get_buffer()->signal_changed().connect([this] {
    if(!interactive_completion)
      return;
    if(CompletionDialog::get() && CompletionDialog::get()->is_visible())
      return;
    if(!has_focus())
      return;
    if(show_parameters)
      autocomplete.stop();
    show_parameters = false;
    delayed_show_arguments_connection.disconnect();
    delayed_show_arguments_connection = Glib::signal_timeout().connect([this]() {
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
  signal_key_press_event().connect([this](GdkEventKey *key) {
    if(show_parameters && CompletionDialog::get() && CompletionDialog::get()->is_visible() &&
       key->keyval != GDK_KEY_Down && key->keyval != GDK_KEY_Up &&
       key->keyval != GDK_KEY_Return && key->keyval != GDK_KEY_KP_Enter &&
       key->keyval != GDK_KEY_ISO_Left_Tab && key->keyval != GDK_KEY_Tab &&
       (key->keyval < GDK_KEY_Shift_L || key->keyval > GDK_KEY_Hyper_R)) {
      get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());
      CompletionDialog::get()->hide();
    }
    return false;
  }, false);

  autocomplete.is_restart_key = [this](guint keyval) {
    auto iter = get_buffer()->get_insert()->get_iter();
    iter.backward_chars(2);
    if(keyval == '.' || (keyval == ':' && *iter == ':') || (keyval == '>' && *iter == '-'))
      return true;
    return false;
  };

  autocomplete.run_check = [this]() {
    auto iter = get_buffer()->get_insert()->get_iter();
    iter.backward_char();
    if(!is_code_iter(iter))
      return false;

    enable_snippets = false;
    show_parameters = false;

    auto line = ' ' + get_line_before();
    const static std::regex regex("^.*([a-zA-Z_\\)\\]\\>]|[^a-zA-Z0-9_][a-zA-Z_][a-zA-Z0-9_]*)(\\.|->)([a-zA-Z0-9_]*)$|" // . or ->
                                  "^.*(::)([a-zA-Z0-9_]*)$|"                                                             // ::
                                  "^.*[^a-zA-Z0-9_]([a-zA-Z_][a-zA-Z0-9_]{2,})$");                                       // part of symbol
    std::smatch sm;
    if(std::regex_match(line, sm, regex)) {
      {
        LockGuard lock(autocomplete.prefix_mutex);
        autocomplete.prefix = sm.length(2) ? sm[3].str() : sm.length(4) ? sm[5].str() : sm[6].str();
        if(!sm.length(2) && !sm.length(4))
          enable_snippets = true;
      }
      return true;
    }
    else if(is_possible_argument()) {
      show_parameters = true;
      LockGuard lock(autocomplete.prefix_mutex);
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
        LockGuard lock(autocomplete.prefix_mutex);
        autocomplete.prefix = get_buffer()->get_text(iter, end_iter);
      }
      auto prev1 = iter;
      if(prev1.backward_char() && *prev1 != '.') {
        auto prev2 = prev1;
        if(!prev2.backward_char())
          enable_snippets = true;
        else if(!(*prev2 == ':' && *prev1 == ':') && !(*prev2 == '-' && *prev1 == '>'))
          enable_snippets = true;
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
    Terminal::get().print("Error: autocomplete failed, reparsing " + this->file_path.string() + '\n', true);
    selected_completion_string = nullptr;
    code_complete_results = nullptr;
    full_reparse();
  };

  autocomplete.add_rows = [this](std::string &buffer, int line_number, int column) {
    if(this->language && (this->language->get_id() == "chdr" || this->language->get_id() == "cpphdr"))
      clangmm::remove_include_guard(buffer);
    code_complete_results = std::make_unique<clangmm::CodeCompleteResults>(clang_tu->get_code_completions(buffer, line_number, column));
    if(code_complete_results->cx_results == nullptr) {
      auto expected = ParseState::processing;
      parse_state.compare_exchange_strong(expected, ParseState::restarting);
      return;
    }

    if(autocomplete.state == Autocomplete::State::starting) {
      std::string prefix;
      {
        LockGuard lock(autocomplete.prefix_mutex);
        prefix = autocomplete.prefix;
      }

      completion_strings.clear();
      snippet_inserts.clear();
      snippet_comments.clear();
      for(unsigned i = 0; i < code_complete_results->size(); ++i) {
        auto result = code_complete_results->get(i);
        if(result.available()) {
          std::string text;
          if(show_parameters) {
            class Recursive {
            public:
              static void f(const clangmm::CompletionString &completion_string, std::string &text) {
                for(unsigned i = 0; i < completion_string.get_num_chunks(); ++i) {
                  auto kind = static_cast<clangmm::CompletionChunkKind>(clang_getCompletionChunkKind(completion_string.cx_completion_string, i));
                  if(kind == clangmm::CompletionChunk_Optional)
                    f(clangmm::CompletionString(clang_getCompletionChunkCompletionString(completion_string.cx_completion_string, i)), text);
                  else if(kind == clangmm::CompletionChunk_CurrentParameter) {
                    auto chunk_cstr = clangmm::String(clang_getCompletionChunkText(completion_string.cx_completion_string, i));
                    text += chunk_cstr.c_str;
                  }
                }
              }
            };
            Recursive::f(result, text);
            if(!text.empty()) {
              bool already_added = false;
              for(auto &row : autocomplete.rows) {
                if(row == text) {
                  already_added = true;
                  break;
                }
              }
              if(!already_added) {
                autocomplete.rows.emplace_back(std::move(text));
                completion_strings.emplace_back(result.cx_completion_string);
              }
            }
          }
          else {
            std::string return_text;
            bool match = false;
            for(unsigned i = 0; i < result.get_num_chunks(); ++i) {
              auto kind = static_cast<clangmm::CompletionChunkKind>(clang_getCompletionChunkKind(result.cx_completion_string, i));
              if(kind != clangmm::CompletionChunk_Informative) {
                auto chunk_cstr = clangmm::String(clang_getCompletionChunkText(result.cx_completion_string, i));
                if(kind == clangmm::CompletionChunk_TypedText) {
                  if(strlen(chunk_cstr.c_str) >= prefix.size() && prefix.compare(0, prefix.size(), chunk_cstr.c_str, prefix.size()) == 0)
                    match = true;
                  else
                    break;
                }
                if(kind == clangmm::CompletionChunk_ResultType)
                  return_text = std::string("  →  ") + chunk_cstr.c_str;
                else
                  text += chunk_cstr.c_str;
              }
            }
            if(match && !text.empty()) {
              if(!return_text.empty())
                text += return_text;
              autocomplete.rows.emplace_back(std::move(text));
              completion_strings.emplace_back(result.cx_completion_string);
            }
          }
        }
      }
      if(!show_parameters && enable_snippets) {
        LockGuard lock(snippets_mutex);
        if(snippets) {
          for(auto &snippet : *snippets) {
            if(prefix.compare(0, prefix.size(), snippet.prefix, 0, prefix.size()) == 0) {
              autocomplete.rows.emplace_back(snippet.prefix);
              completion_strings.emplace_back(nullptr);
              snippet_inserts.emplace(autocomplete.rows.size() - 1, snippet.body);
              snippet_comments.emplace(autocomplete.rows.size() - 1, snippet.description);
            }
          }
        }
      }
    }
  };

  autocomplete.on_show = [this] {
    hide_tooltips();
  };

  autocomplete.on_hide = [this] {
    selected_completion_string = nullptr;
    code_complete_results = nullptr;
  };

  autocomplete.on_changed = [this](unsigned int index, const std::string &text) {
    selected_completion_string = completion_strings[index];
  };

  autocomplete.on_select = [this](unsigned int index, const std::string &text, bool hide_window) {
    if(!completion_strings[index]) { // Insert snippet instead
      get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());

      if(!hide_window) {
        get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), text);
      }
      else
        insert_snippet(CompletionDialog::get()->start_mark->get_iter(), snippet_inserts[index]);
      return;
    }

    std::string row;
    auto pos = text.find("  →  ");
    if(pos != std::string::npos)
      row = text.substr(0, pos);
    else
      row = text;
    //erase existing variable or function before insert iter
    get_buffer()->erase(CompletionDialog::get()->start_mark->get_iter(), get_buffer()->get_insert()->get_iter());
    //do not insert template argument or function parameters if they already exist
    auto iter = get_buffer()->get_insert()->get_iter();
    if(*iter == '<' || *iter == '(') {
      auto bracket_pos = row.find(*iter);
      if(bracket_pos != std::string::npos) {
        row = row.substr(0, bracket_pos);
      }
    }
    //Fixes for the most commonly used stream manipulators
    auto manipulators_map = autocomplete_manipulators_map();
    auto it = manipulators_map.find(row);
    if(it != manipulators_map.end())
      row = it->second;
    //Do not insert template argument, function parameters or ':' unless hide_window is true
    if(!hide_window) {
      for(size_t c = 0; c < row.size(); ++c) {
        if(row[c] == '<' || row[c] == '(' || row[c] == ':') {
          row.erase(c);
          break;
        }
      }
    }
    // Do not insert last " or > inside #include statements
    if(!row.empty() && ((*iter == '"' && row.back() == '"' && row.find('"') == row.size() - 1) || (*iter == '>' && row.back() == '>' && row.find('<') == std::string::npos)))
      row.pop_back();

    get_buffer()->insert(CompletionDialog::get()->start_mark->get_iter(), row);
    //if selection is finalized, select text inside template arguments or function parameters
    if(hide_window) {
      size_t start_pos = std::string::npos;
      size_t end_pos = std::string::npos;
      if(show_parameters) {
        start_pos = 0;
        end_pos = row.size();
      }
      else {
        auto para_pos = row.find('(');
        auto angle_pos = row.find('<');
        if(angle_pos < para_pos) {
          start_pos = angle_pos + 1;
          end_pos = row.find('>');
        }
        else if(para_pos != std::string::npos) {
          start_pos = para_pos + 1;
          end_pos = row.size() - 1;
        }
        if(start_pos == std::string::npos || end_pos == std::string::npos) {
          if((start_pos = row.find('\"')) != std::string::npos) {
            end_pos = row.find('\"', start_pos + 1);
            ++start_pos;
          }
        }
      }
      if(start_pos == std::string::npos || end_pos == std::string::npos) {
        if((start_pos = row.find(' ')) != std::string::npos) {
          std::vector<std::string> parameters = {"expression", "arguments", "identifier", "type name", "qualifier::name", "macro", "condition"};
          for(auto &parameter : parameters) {
            if((start_pos = row.find(parameter, start_pos + 1)) != std::string::npos) {
              end_pos = start_pos + parameter.size();
              break;
            }
          }
        }
      }

      if(start_pos != std::string::npos && end_pos != std::string::npos) {
        int start_offset = CompletionDialog::get()->start_mark->get_iter().get_offset() + start_pos;
        int end_offset = CompletionDialog::get()->start_mark->get_iter().get_offset() + end_pos;
        auto size = get_buffer()->size();
        if(start_offset != end_offset && start_offset < size && end_offset < size)
          get_buffer()->select_range(get_buffer()->get_iter_at_offset(start_offset), get_buffer()->get_iter_at_offset(end_offset));
      }
      else {
        //new autocomplete after for instance when selecting "std::"
        auto iter = get_buffer()->get_insert()->get_iter();
        if(iter.backward_char() && *iter == ':') {
          autocomplete.run();
          return;
        }
      }
    }
  };

  autocomplete.get_tooltip = [this](unsigned int index) {
    return completion_strings[index] ? clangmm::to_string(clang_getCompletionBriefComment(completion_strings[index])) : snippet_comments[index];
  };
}

const std::unordered_map<std::string, std::string> &Source::ClangViewAutocomplete::autocomplete_manipulators_map() {
  //TODO: feel free to add more
  static std::unordered_map<std::string, std::string> map = {
      {"endl(basic_ostream<_CharT, _Traits> &__os)", "endl"},
      {"flush(basic_ostream<_CharT, _Traits> &__os)", "flush"},
      {"hex(std::ios_base &__str)", "hex"},  //clang++ headers
      {"hex(std::ios_base &__base)", "hex"}, //g++ headers
      {"dec(std::ios_base &__str)", "dec"},
      {"dec(std::ios_base &__base)", "dec"}
  };
  return map;
}


Source::ClangViewRefactor::ClangViewRefactor(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language)
    : BaseView(file_path, language), Source::ClangViewParse(file_path, language) {
  get_token_spelling = [this]() {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return std::string();
    }
    auto identifier = get_identifier();
    if(identifier.spelling.empty() ||
       identifier.spelling == "::" || identifier.spelling == "," || identifier.spelling == "=" ||
       identifier.spelling == "(" || identifier.spelling == ")" ||
       identifier.spelling == "[" || identifier.spelling == "]") {
      Info::get().print("No valid symbol found at current cursor location");
      return std::string();
    }
    return identifier.spelling;
  };

  rename_similar_tokens = [this](const std::string &text) {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return;
    }
    auto identifier = get_identifier();
    if(identifier) {
      wait_parsing();

      std::vector<clangmm::TranslationUnit *> translation_units;
      translation_units.emplace_back(clang_tu.get());
      for(auto &view : views) {
        if(view != this) {
          if(auto clang_view = dynamic_cast<Source::ClangView *>(view))
            translation_units.emplace_back(clang_view->clang_tu.get());
        }
      }

      auto build = Project::Build::create(this->file_path);
      auto usages = Usages::Clang::get_usages(build->project_path, build->get_default_path(), build->get_debug_path(), identifier.spelling, identifier.cursor, translation_units);

      std::vector<Source::View *> renamed_views;
      std::vector<Usages::Clang::Usages *> usages_renamed;
      for(auto &usage : usages) {
        size_t line_c = usage.lines.size() - 1;
        auto view_it = views.end();
        for(auto it = views.begin(); it != views.end(); ++it) {
          if((*it)->file_path == usage.path) {
            view_it = it;
            break;
          }
        }
        if(view_it != views.end()) {
          (*view_it)->get_buffer()->begin_user_action();
          for(auto offset_it = usage.offsets.rbegin(); offset_it != usage.offsets.rend(); ++offset_it) {
            auto start_iter = (*view_it)->get_buffer()->get_iter_at_line_index(offset_it->first.line - 1, offset_it->first.index - 1);
            auto end_iter = (*view_it)->get_buffer()->get_iter_at_line_index(offset_it->second.line - 1, offset_it->second.index - 1);
            (*view_it)->get_buffer()->erase(start_iter, end_iter);
            start_iter = (*view_it)->get_buffer()->get_iter_at_line_index(offset_it->first.line - 1, offset_it->first.index - 1);
            (*view_it)->get_buffer()->insert(start_iter, text);
            if(offset_it->first.index - 1 < usage.lines[line_c].size())
              usage.lines[line_c].replace(offset_it->first.index - 1, offset_it->second.index - offset_it->first.index, text);
            --line_c;
          }
          (*view_it)->get_buffer()->end_user_action();
          (*view_it)->save();
          renamed_views.emplace_back(*view_it);
          usages_renamed.emplace_back(&usage);
        }
        else {
          std::string buffer;
          {
            std::ifstream stream(usage.path.string(), std::ifstream::binary);
            if(stream)
              buffer.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
          }
          std::ofstream stream(usage.path.string(), std::ifstream::binary);
          if(!buffer.empty() && stream) {
            std::vector<size_t> lines_start_pos = {0};
            for(size_t c = 0; c < buffer.size(); ++c) {
              if(buffer[c] == '\n')
                lines_start_pos.emplace_back(c + 1);
            }
            for(auto offset_it = usage.offsets.rbegin(); offset_it != usage.offsets.rend(); ++offset_it) {
              auto start_line = offset_it->first.line - 1;
              auto end_line = offset_it->second.line - 1;
              if(start_line < lines_start_pos.size() && end_line < lines_start_pos.size()) {
                auto start = lines_start_pos[start_line] + offset_it->first.index - 1;
                auto end = lines_start_pos[end_line] + offset_it->second.index - 1;
                if(start < buffer.size() && end <= buffer.size())
                  buffer.replace(start, end - start, text);
              }
              if(offset_it->first.index - 1 < usage.lines[line_c].size())
                usage.lines[line_c].replace(offset_it->first.index - 1, offset_it->second.index - offset_it->first.index, text);
              --line_c;
            }
            stream.write(buffer.data(), buffer.size());
            usages_renamed.emplace_back(&usage);
          }
          else
            Terminal::get().print("Error: could not write to file " + usage.path.string() + '\n', true);
        }
      }

      if(!usages_renamed.empty()) {
        Terminal::get().print("Renamed ");
        Terminal::get().print(identifier.spelling, true);
        Terminal::get().print(" to ");
        Terminal::get().print(text, true);
        Terminal::get().print(" at:\n");
      }
      for(auto &usage : usages_renamed) {
        size_t line_c = 0;
        for(auto &offset : usage->offsets) {
          Terminal::get().print(filesystem::get_short_path(usage->path).string() + ':' + std::to_string(offset.first.line) + ':' + std::to_string(offset.first.index) + ": ");
          auto &line = usage->lines[line_c];
          auto index = offset.first.index - 1;
          unsigned start = 0;
          for(auto &chr : line) {
            if(chr != ' ' && chr != '\t')
              break;
            ++start;
          }
          if(start < line.size() && index + text.size() < line.size()) {
            Terminal::get().print(line.substr(start, index - start));
            Terminal::get().print(line.substr(index, text.size()), true);
            Terminal::get().print(line.substr(index + text.size()));
          }
          Terminal::get().print("\n");
          ++line_c;
        }
      }

      for(auto &view : renamed_views)
        view->soft_reparse_needed = false;
    }
  };

  auto declaration_location = [this]() {
    auto identifier = get_identifier();
    if(identifier) {
      auto source_location = identifier.cursor.get_canonical().get_source_location();
      auto offset = source_location.get_offset();
      return Offset(offset.line - 1, offset.index - 1, source_location.get_path());
    }
    else {
      // If cursor is at an include line, return offset to included file
      std::smatch sm;
      auto line = get_line();
      if(std::regex_match(line, sm, include_regex)) {
        struct ClientData {
          boost::filesystem::path &file_path;
          std::string found_include;
          int line_nr;
          std::string sm_str;
        };
        ClientData client_data{this->file_path, std::string(), get_buffer()->get_insert()->get_iter().get_line(), sm[1].str()};

        // Attempt to find the 100% correct include file first
        clang_getInclusions(clang_tu->cx_tu, [](CXFile included_file, CXSourceLocation *inclusion_stack, unsigned include_len, CXClientData client_data_) {
          auto client_data = static_cast<ClientData *>(client_data_);
          if(client_data->found_include.empty() && include_len > 0) {
            auto source_location = clangmm::SourceLocation(inclusion_stack[0]);
            if(static_cast<int>(source_location.get_offset().line) - 1 == client_data->line_nr &&
               filesystem::get_normal_path(source_location.get_path()) == client_data->file_path)
              client_data->found_include = clangmm::to_string(clang_getFileName(included_file));
          }
        }, &client_data);

        if(!client_data.found_include.empty())
          return Offset(0, 0, client_data.found_include);

        // Find a matching include file if no include was found previously
        clang_getInclusions(clang_tu->cx_tu, [](CXFile included_file, CXSourceLocation *inclusion_stack, unsigned include_len, CXClientData client_data_) {
          auto client_data = static_cast<ClientData *>(client_data_);
          if(client_data->found_include.empty()) {
            for(unsigned c = 1; c < include_len; ++c) {
              auto source_location = clangmm::SourceLocation(inclusion_stack[c]);
              if(static_cast<int>(source_location.get_offset().line) - 1 <= client_data->line_nr &&
                 filesystem::get_normal_path(source_location.get_path()) == client_data->file_path) {
                auto included_file_str = clangmm::to_string(clang_getFileName(included_file));
                if(included_file_str.size() >= client_data->sm_str.size() &&
                   included_file_str.compare(included_file_str.size() - client_data->sm_str.size(), client_data->sm_str.size(), client_data->sm_str) == 0) {
                  client_data->found_include = included_file_str;
                  break;
                }
              }
            }
          }
        }, &client_data);

        if(!client_data.found_include.empty())
          return Offset(0, 0, client_data.found_include);
      }
    }
    return Offset();
  };

  get_declaration_location = [this, declaration_location]() {
    if(!parsed) {
      if(selected_completion_string) {
        auto completion_cursor = clangmm::CompletionString(selected_completion_string).get_cursor(clang_tu->cx_tu);
        if(completion_cursor) {
          auto source_location = completion_cursor.get_source_location();
          auto source_location_offset = source_location.get_offset();
          if(CompletionDialog::get())
            CompletionDialog::get()->hide();
          auto offset = Offset(source_location_offset.line - 1, source_location_offset.index - 1, source_location.get_path());

          // Workaround for bug in ArchLinux's clang_getFileName()
          // TODO: remove the workaround when this is fixed
          auto include_path = filesystem::get_normal_path(offset.file_path);
          boost::system::error_code ec;
          if(!boost::filesystem::exists(include_path, ec))
            offset.file_path = "/usr/include" / include_path;

          return offset;
        }
        else {
          Info::get().print("No declaration found");
          return Offset();
        }
      }

      Info::get().print("Buffer is parsing");
      return Offset();
    }
    auto offset = declaration_location();
    if(!offset)
      Info::get().print("No declaration found");

    // Workaround for bug in ArchLinux's clang_getFileName()
    // TODO: remove the workaround when this is fixed
    auto include_path = filesystem::get_normal_path(offset.file_path);
    boost::system::error_code ec;
    if(!boost::filesystem::exists(include_path, ec))
      offset.file_path = "/usr/include" / include_path;

    return offset;
  };

  get_type_declaration_location = [this]() {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return Offset();
    }
    auto identifier = get_identifier();
    if(identifier) {
      auto type_cursor = identifier.cursor.get_type().get_cursor();
      if(type_cursor) {
        auto source_location = type_cursor.get_source_location();
        auto path = source_location.get_path();
        if(!path.empty()) {
          auto source_location_offset = source_location.get_offset();
          auto offset = Offset(source_location_offset.line - 1, source_location_offset.index - 1, path);

          // Workaround for bug in ArchLinux's clang_getFileName()
          // TODO: remove the workaround when this is fixed
          auto include_path = filesystem::get_normal_path(offset.file_path);
          boost::system::error_code ec;
          if(!boost::filesystem::exists(include_path, ec))
            offset.file_path = "/usr/include" / include_path;

          return offset;
        }
      }
    }
    Info::get().print("No type declaration found");
    return Offset();
  };

  auto implementation_locations = [this](const Identifier &identifier) {
    std::vector<Offset> offsets;
    if(identifier) {
      wait_parsing();

      //First, look for a definition cursor that is equal
      auto identifier_usr = identifier.cursor.get_usr();
      for(auto &view : views) {
        if(auto clang_view = dynamic_cast<Source::ClangView *>(view)) {
          for(auto &token : *clang_view->clang_tokens) {
            auto cursor = token.get_cursor();
            auto cursor_kind = cursor.get_kind();
            if((cursor_kind == clangmm::Cursor::Kind::FunctionDecl || cursor_kind == clangmm::Cursor::Kind::CXXMethod ||
                cursor_kind == clangmm::Cursor::Kind::Constructor || cursor_kind == clangmm::Cursor::Kind::Destructor ||
                cursor_kind == clangmm::Cursor::Kind::FunctionTemplate || cursor_kind == clangmm::Cursor::Kind::ConversionFunction) &&
               token.is_identifier()) {
              auto token_spelling = token.get_spelling();
              if(identifier.kind == cursor.get_kind() && identifier.spelling == token_spelling && identifier_usr == cursor.get_usr()) {
                if(clang_isCursorDefinition(cursor.cx_cursor)) {
                  Offset offset;
                  auto location = cursor.get_source_location();
                  auto clang_offset = location.get_offset();
                  offset.file_path = location.get_path();
                  offset.line = clang_offset.line - 1;
                  offset.index = clang_offset.index - 1;
                  offsets.emplace_back(offset);
                }
              }
            }
          }
        }
      }
      if(!offsets.empty())
        return offsets;

      //If no implementation was found, try using clang_getCursorDefinition
      auto definition = identifier.cursor.get_definition();
      if(definition) {
        auto location = definition.get_source_location();
        Offset offset;
        offset.file_path = location.get_path();
        auto clang_offset = location.get_offset();
        offset.line = clang_offset.line - 1;
        offset.index = clang_offset.index - 1;
        offsets.emplace_back(offset);
        return offsets;
      }

      //If no implementation was found, use declaration if it is a function template
      auto canonical = identifier.cursor.get_canonical();
      auto cursor = clang_tu->get_cursor(canonical.get_source_location());
      if(cursor && cursor.get_kind() == clangmm::Cursor::Kind::FunctionTemplate) {
        auto location = cursor.get_source_location();
        Offset offset;
        offset.file_path = location.get_path();
        auto clang_offset = location.get_offset();
        offset.line = clang_offset.line - 1;
        offset.index = clang_offset.index - 1;
        offsets.emplace_back(offset);
        return offsets;
      }

      //If no implementation was found, try using Ctags
      auto name = identifier.cursor.get_spelling();
      auto parent = identifier.cursor.get_semantic_parent();
      while(parent && parent.get_kind() != clangmm::Cursor::Kind::TranslationUnit) {
        auto spelling = parent.get_spelling() + "::";
        name.insert(0, spelling);
        parent = parent.get_semantic_parent();
      }
      auto ctags_locations = Ctags::get_locations(this->file_path.parent_path(), name, identifier.cursor.get_type_description());
      if(!ctags_locations.empty()) {
        for(auto &ctags_location : ctags_locations) {
          Offset offset;
          offset.file_path = ctags_location.file_path;
          offset.line = ctags_location.line;
          offset.index = ctags_location.index;
          offsets.emplace_back(offset);
        }
        return offsets;
      }
    }
    return offsets;
  };

  get_implementation_locations = [this, implementation_locations]() {
    if(!parsed) {
      if(selected_completion_string) {
        auto completion_cursor = clangmm::CompletionString(selected_completion_string).get_cursor(clang_tu->cx_tu);
        if(completion_cursor) {
          auto offsets = implementation_locations(Identifier(completion_cursor.get_token_spelling(), completion_cursor));
          if(offsets.empty()) {
            Info::get().print("No implementation found");
            return std::vector<Offset>();
          }
          if(CompletionDialog::get())
            CompletionDialog::get()->hide();

          // Workaround for bug in ArchLinux's clang_getFileName()
          // TODO: remove the workaround when this is fixed
          for(auto &offset : offsets) {
            auto include_path = filesystem::get_normal_path(offset.file_path);
            boost::system::error_code ec;
            if(!boost::filesystem::exists(include_path, ec))
              offset.file_path = "/usr/include" / include_path;
          }

          return offsets;
        }
        else {
          Info::get().print("No implementation found");
          return std::vector<Offset>();
        }
      }

      Info::get().print("Buffer is parsing");
      return std::vector<Offset>();
    }
    auto offsets = implementation_locations(get_identifier());
    if(offsets.empty())
      Info::get().print("No implementation found");

    // Workaround for bug in ArchLinux's clang_getFileName()
    // TODO: remove the workaround when this is fixed
    for(auto &offset : offsets) {
      auto include_path = filesystem::get_normal_path(offset.file_path);
      boost::system::error_code ec;
      if(!boost::filesystem::exists(include_path, ec))
        offset.file_path = "/usr/include" / include_path;
    }

    return offsets;
  };

  get_declaration_or_implementation_locations = [this, declaration_location, implementation_locations]() {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return std::vector<Offset>();
    }

    std::vector<Offset> offsets;

    bool is_implementation = false;
    auto iter = get_buffer()->get_insert()->get_iter();
    auto line = static_cast<unsigned>(iter.get_line());
    auto index = static_cast<unsigned>(iter.get_line_index());
    for(size_t c = 0; c < clang_tokens->size(); ++c) {
      auto &token = (*clang_tokens)[c];
      if(token.is_identifier()) {
        auto &token_offsets = clang_tokens_offsets[c];
        if(line == token_offsets.first.line - 1 && index >= token_offsets.first.index - 1 && index <= token_offsets.second.index - 1) {
          if(clang_isCursorDefinition(token.get_cursor().cx_cursor) > 0)
            is_implementation = true;
          break;
        }
      }
    }
    // If cursor is at implementation, return declaration_location
    if(is_implementation) {
      auto offset = declaration_location();
      if(offset)
        offsets.emplace_back(offset);
    }
    else {
      auto implementation_offsets = implementation_locations(get_identifier());
      if(!implementation_offsets.empty()) {
        offsets = std::move(implementation_offsets);
      }
      else {
        auto offset = declaration_location();
        if(offset)
          offsets.emplace_back(offset);
      }
    }

    if(offsets.empty())
      Info::get().print("No declaration or implementation found");

    // Workaround for bug in ArchLinux's clang_getFileName()
    // TODO: remove the workaround when this is fixed
    for(auto &offset : offsets) {
      auto include_path = filesystem::get_normal_path(offset.file_path);
      boost::system::error_code ec;
      if(!boost::filesystem::exists(include_path, ec))
        offset.file_path = "/usr/include" / include_path;
    }

    return offsets;
  };

  get_usages = [this]() {
    std::vector<std::pair<Offset, std::string>> usages;
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return usages;
    }
    auto identifier = get_identifier();
    if(identifier) {
      wait_parsing();

      auto embolden_token = [](std::string &line, unsigned token_start_pos, unsigned token_end_pos) {
        //markup token as bold
        size_t pos = 0;
        while((pos = line.find('&', pos)) != std::string::npos) {
          size_t pos2 = line.find(';', pos + 2);
          if(token_start_pos > pos) {
            token_start_pos += pos2 - pos;
            token_end_pos += pos2 - pos;
          }
          else if(token_end_pos > pos)
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
      };

      std::vector<clangmm::TranslationUnit *> translation_units;
      translation_units.emplace_back(clang_tu.get());
      for(auto &view : views) {
        if(view != this) {
          if(auto clang_view = dynamic_cast<Source::ClangView *>(view))
            translation_units.emplace_back(clang_view->clang_tu.get());
        }
      }

      auto build = Project::Build::create(this->file_path);
      auto usages_clang = Usages::Clang::get_usages(build->project_path, build->get_default_path(), build->get_debug_path(), {identifier.spelling}, {identifier.cursor}, translation_units);
      for(auto &usage : usages_clang) {
        for(size_t c = 0; c < usage.offsets.size(); ++c) {
          std::string line = Glib::Markup::escape_text(usage.lines[c]);
          embolden_token(line, usage.offsets[c].first.index - 1, usage.offsets[c].second.index - 1);
          usages.emplace_back(Offset(usage.offsets[c].first.line - 1, usage.offsets[c].first.index - 1, usage.path), line);
        }
      }
    }

    if(usages.empty())
      Info::get().print("No symbol found at current cursor location");
    return usages;
  };

  get_method = [this] {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return std::string();
    }
    auto iter = get_buffer()->get_insert()->get_iter();
    auto line = static_cast<unsigned>(iter.get_line());
    auto index = static_cast<unsigned>(iter.get_line_index());
    for(size_t c = clang_tokens->size() - 1; c != static_cast<size_t>(-1); --c) {
      auto &token = (*clang_tokens)[c];
      if(token.is_identifier()) {
        auto &token_offsets = clang_tokens_offsets[c];
        if(line == token_offsets.first.line - 1 && index >= token_offsets.first.index - 1 && index <= token_offsets.second.index - 1) {
          auto token_spelling = token.get_spelling();
          if(!token_spelling.empty() &&
             (token_spelling.size() > 1 || (token_spelling.back() >= 'a' && token_spelling.back() <= 'z') ||
              (token_spelling.back() >= 'A' && token_spelling.back() <= 'Z') ||
              token_spelling.back() == '_')) {
            auto cursor = token.get_cursor();
            auto kind = cursor.get_kind();
            if(kind == clangmm::Cursor::Kind::FunctionDecl || kind == clangmm::Cursor::Kind::CXXMethod ||
               kind == clangmm::Cursor::Kind::Constructor || kind == clangmm::Cursor::Kind::Destructor ||
               kind == clangmm::Cursor::Kind::ConversionFunction) {
              auto referenced = cursor.get_referenced();
              if(referenced && referenced == cursor) {
                std::string result;
                std::string specifier;
                if(kind == clangmm::Cursor::Kind::FunctionDecl || kind == clangmm::Cursor::Kind::CXXMethod) {
                  auto start_offset = cursor.get_source_range().get_start().get_offset();
                  auto end_offset = token_offsets.first;

                  // To accurately get result type with needed namespace and class/struct names:
                  int angle_brackets = 0;
                  for(size_t c = 0; c < clang_tokens->size(); ++c) {
                    auto &token = (*clang_tokens)[c];
                    auto &token_offsets = clang_tokens_offsets[c];
                    if((token_offsets.first.line == start_offset.line && token_offsets.second.line != end_offset.line && token_offsets.first.index >= start_offset.index) ||
                       (token_offsets.first.line > start_offset.line && token_offsets.second.line < end_offset.line) ||
                       (token_offsets.first.line != start_offset.line && token_offsets.second.line == end_offset.line && token_offsets.second.index <= end_offset.index) ||
                       (token_offsets.first.line == start_offset.line && token_offsets.second.line == end_offset.line &&
                        token_offsets.first.index >= start_offset.index && token_offsets.second.index <= end_offset.index)) {
                      auto token_spelling = token.get_spelling();
                      if(token.get_kind() == clangmm::Token::Kind::Identifier) {
                        if(c == 0 || (*clang_tokens)[c - 1].get_spelling() != "::") {
                          auto name = token_spelling;
                          auto parent = token.get_cursor().get_type().get_cursor().get_semantic_parent();
                          while(parent && parent.get_kind() != clangmm::Cursor::Kind::TranslationUnit) {
                            auto spelling = parent.get_token_spelling();
                            name.insert(0, spelling + "::");
                            parent = parent.get_semantic_parent();
                          }
                          result += name;
                        }
                        else
                          result += token_spelling;
                      }
                      else if((token_spelling == "*" || token_spelling == "&") && !result.empty() && result.back() != '*' && result.back() != '&')
                        result += ' ' + token_spelling;
                      else if(token_spelling == "extern" || token_spelling == "static" || token_spelling == "virtual" || token_spelling == "friend")
                        continue;
                      else if(token_spelling == "," || (token_spelling.size() > 1 && token_spelling != "::" && angle_brackets == 0))
                        result += token_spelling + ' ';
                      else {
                        if(token_spelling == "<")
                          ++angle_brackets;
                        else if(token_spelling == ">")
                          --angle_brackets;
                        result += token_spelling;
                      }
                    }
                  }

                  if(!result.empty() && result.back() != '*' && result.back() != '&' && result.back() != ' ')
                    result += ' ';

                  if(clang_CXXMethod_isConst(cursor.cx_cursor))
                    specifier += " const";

#if CINDEX_VERSION_MAJOR > 0 || (CINDEX_VERSION_MAJOR == 0 && CINDEX_VERSION_MINOR >= 43)
                  auto exception_specification_kind = static_cast<CXCursor_ExceptionSpecificationKind>(clang_getCursorExceptionSpecificationType(cursor.cx_cursor));
                  if(exception_specification_kind == CXCursor_ExceptionSpecificationKind_BasicNoexcept)
                    specifier += " noexcept";
#endif
                }

                auto name = cursor.get_spelling();
                auto parent = cursor.get_semantic_parent();
                std::vector<std::string> semantic_parents;
                while(parent && parent.get_kind() != clangmm::Cursor::Kind::TranslationUnit) {
                  auto spelling = parent.get_spelling() + "::";
                  if(spelling != "::") {
                    semantic_parents.emplace_back(spelling);
                    name.insert(0, spelling);
                  }
                  parent = parent.get_semantic_parent();
                }

                std::string arguments;
                for(auto &argument_cursor : cursor.get_arguments()) {
                  auto argument_type = argument_cursor.get_type().get_spelling();
                  for(auto it = semantic_parents.rbegin(); it != semantic_parents.rend(); ++it) {
                    size_t pos = argument_type.find(*it);
                    if(pos == 0 || (pos != std::string::npos && argument_type[pos - 1] == ' '))
                      argument_type.erase(pos, it->size());
                  }
                  auto argument = argument_cursor.get_spelling();
                  if(!arguments.empty())
                    arguments += ", ";
                  arguments += argument_type;
                  if(!arguments.empty() && arguments.back() != '*' && arguments.back() != '&')
                    arguments += ' ';
                  arguments += argument;
                }
                return result + name + '(' + arguments + ")" + specifier + " {}";
              }
            }
          }
        }
      }
    }
    Info::get().print("No method found at current cursor location");
    return std::string();
  };

  if(language && (language->get_id() == "chdr" || language->get_id() == "cpphdr")) {
    get_methods = [this]() {
      std::vector<std::pair<Offset, std::string>> methods;
      if(!parsed) {
        Info::get().print("Buffer is parsing");
        return methods;
      }
      clangmm::Offset last_offset{static_cast<unsigned>(-1), static_cast<unsigned>(-1)};
      for(auto &token : *clang_tokens) {
        if(token.is_identifier()) {
          auto cursor = token.get_cursor();
          auto kind = cursor.get_kind();
          if(kind == clangmm::Cursor::Kind::FunctionDecl || kind == clangmm::Cursor::Kind::CXXMethod ||
             kind == clangmm::Cursor::Kind::Constructor || kind == clangmm::Cursor::Kind::Destructor ||
             kind == clangmm::Cursor::Kind::FunctionTemplate || kind == clangmm::Cursor::Kind::ConversionFunction) {
            auto offset = cursor.get_source_location().get_offset();
            if(offset == last_offset)
              continue;
            last_offset = offset;

            std::string scope;
            auto parent = cursor.get_semantic_parent();
            while(parent && parent.get_kind() != clangmm::Cursor::Kind::TranslationUnit) {
              scope.insert(0, parent.get_display_name() + (scope.empty() ? "" : "::"));
              parent = parent.get_semantic_parent();
            }
            if(!scope.empty())
              scope += "::";
            scope = Glib::Markup::escape_text(scope);

            std::string ret;
            if(kind != clangmm::Cursor::Kind::Constructor && kind != clangmm::Cursor::Kind::Destructor) {
              ret += cursor.get_type().get_result().get_spelling();
              if(!ret.empty() && ret.back() != '&' && ret.back() != '*')
                ret += ' ';
            }
            ret = Glib::Markup::escape_text(ret);

            auto method = Glib::Markup::escape_text(cursor.get_display_name());
            // Add bold method token
            auto end = method.find('(');
            if(end == std::string::npos)
              continue;
            method.insert(end, "</b>");
            method.insert(0, "<b>");

            methods.emplace_back(Offset(offset.line - 1, offset.index - 1), std::to_string(offset.line) + ": " + ret + scope + method);
          }
        }
      }
      if(methods.empty())
        Info::get().print("No methods found in current buffer");

      return methods;
    };
  }

  get_token_data = [this]() -> std::vector<std::string> {
    clangmm::Cursor cursor;

    std::vector<std::string> data;
    if(!parsed) {
      if(selected_completion_string) {
        cursor = clangmm::CompletionString(selected_completion_string).get_cursor(clang_tu->cx_tu);
        if(!cursor) {
          Info::get().print("No symbol found");
          return data;
        }
      }
      else {
        Info::get().print("Buffer is parsing");
        return data;
      }
    }

    if(!cursor) {
      auto identifier = get_identifier();
      if(identifier)
        cursor = identifier.cursor.get_canonical();
    }

    if(cursor) {
      data.emplace_back("clang");

      std::string symbol;
      clangmm::Cursor last_cursor;
      auto it = data.end();
      do {
        auto token_spelling = cursor.get_token_spelling();
        if(!token_spelling.empty() && token_spelling != "__1" && token_spelling.compare(0, 5, "__cxx") != 0) {
          it = data.emplace(it, token_spelling);
          if(symbol.empty())
            symbol = token_spelling;
          else
            symbol.insert(0, token_spelling + "::");
        }
        last_cursor = cursor;
        cursor = cursor.get_semantic_parent();
      } while(cursor && cursor.get_kind() != clangmm::Cursor::Kind::TranslationUnit);

      if(last_cursor.get_kind() != clangmm::Cursor::Kind::Namespace)
        data.emplace(++data.begin(), "");

      auto url = Documentation::CppReference::get_url(symbol);
      if(!url.empty())
        return {url};
    }

    if(data.empty())
      Info::get().print("No symbol found at current cursor location");

    return data;
  };

  goto_next_diagnostic = [this]() {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return;
    }
    place_cursor_at_next_diagnostic();
  };

  get_fix_its = [this]() {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return std::vector<FixIt>();
    }
    if(fix_its.empty())
      Info::get().print("No fix-its found in current buffer");
    return fix_its;
  };

  get_documentation_template = [this]() {
    if(!parsed) {
      Info::get().print("Buffer is parsing");
      return std::tuple<Source::Offset, std::string, size_t>(Source::Offset(), "", 0);
    }
    auto identifier = get_identifier();
    if(identifier) {
      auto cursor = identifier.cursor.get_canonical();
      if(!clang_Range_isNull(clang_Cursor_getCommentRange(cursor.cx_cursor))) {
        Info::get().print("Symbol is already documented");
        return std::tuple<Source::Offset, std::string, size_t>(Source::Offset(), "", 0);
      }
      auto clang_offsets = cursor.get_source_range().get_offsets();
      auto source_offset = Offset(clang_offsets.first.line - 1, 0, cursor.get_source_location().get_path());
      std::string tabs;
      for(size_t c = 0; c < clang_offsets.first.index - 1; ++c)
        tabs += ' ';
      auto first_line = tabs + "/**\n";
      auto second_line = tabs + " * \n";
      auto iter_offset = first_line.size() + second_line.size() - 1;

      std::string param_lines;
      for(int c = 0; c < clang_Cursor_getNumArguments(cursor.cx_cursor); ++c)
        param_lines += tabs + " * @param " + clangmm::Cursor(clang_Cursor_getArgument(cursor.cx_cursor, c)).get_spelling() + '\n';

      std::string return_line;
      auto return_spelling = cursor.get_type().get_result().get_spelling();
      if(!return_spelling.empty() && return_spelling != "void")
        return_line += tabs + " * @return\n";

      auto documentation = first_line + second_line;
      if(!param_lines.empty() || !return_line.empty())
        documentation += tabs + " *\n";

      documentation += param_lines + return_line + tabs + " */\n";

      return std::tuple<Source::Offset, std::string, size_t>(source_offset, documentation, iter_offset);
    }
    else {
      Info::get().print("No symbol found at current cursor location");
      return std::tuple<Source::Offset, std::string, size_t>(Source::Offset(), "", 0);
    }
  };
}

Source::ClangViewRefactor::Identifier Source::ClangViewRefactor::get_identifier() {
  if(!parsed)
    return Identifier();
  auto iter = get_buffer()->get_insert()->get_iter();
  auto line = static_cast<unsigned>(iter.get_line());
  auto index = static_cast<unsigned>(iter.get_line_index());
  for(size_t c = clang_tokens->size() - 1; c != static_cast<size_t>(-1); --c) {
    auto &token = (*clang_tokens)[c];
    if(token.is_identifier()) {
      auto &token_offsets = clang_tokens_offsets[c];
      if(line == token_offsets.first.line - 1 && index >= token_offsets.first.index - 1 && index <= token_offsets.second.index - 1) {
        auto referenced = token.get_cursor().get_referenced();
        if(referenced)
          return Identifier(token.get_spelling(), referenced);
      }
    }
  }
  return Identifier();
}

void Source::ClangViewRefactor::wait_parsing() {
  std::unique_ptr<Dialog::Message> message;
  std::vector<Source::ClangView *> clang_views;
  for(auto &view : views) {
    if(auto clang_view = dynamic_cast<Source::ClangView *>(view)) {
      if(!clang_view->parsed && !clang_view->selected_completion_string) {
        clang_views.emplace_back(clang_view);
        if(!message)
          message = std::make_unique<Dialog::Message>("Please wait while all buffers finish parsing");
      }
    }
  }
  if(message) {
    for(;;) {
      while(Gtk::Main::events_pending())
        Gtk::Main::iteration();
      bool all_parsed = true;
      for(auto &clang_view : clang_views) {
        if(!clang_view->parsed) {
          all_parsed = false;
          break;
        }
      }
      if(all_parsed)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    message->hide();
  }
}

void Source::ClangViewRefactor::apply_similar_symbol_tag() {
  get_buffer()->remove_tag(similar_symbol_tag, get_buffer()->begin(), get_buffer()->end());
  auto identifier = get_identifier();
  if(identifier) {
    auto offsets = clang_tokens->get_similar_token_offsets(identifier.kind, identifier.spelling, identifier.cursor.get_all_usr_extended());
    for(auto &offset : offsets) {
      auto start_iter = get_buffer()->get_iter_at_line_index(offset.first.line - 1, offset.first.index - 1);
      auto end_iter = get_buffer()->get_iter_at_line_index(offset.second.line - 1, offset.second.index - 1);
      get_buffer()->apply_tag(similar_symbol_tag, start_iter, end_iter);
    }
  }
}

void Source::ClangViewRefactor::apply_clickable_tag(const Gtk::TextIter &iter) {
  if(!parsed)
    return;
  auto line = static_cast<unsigned>(iter.get_line());
  auto index = static_cast<unsigned>(iter.get_line_index());

  for(size_t c = clang_tokens->size() - 1; c != static_cast<size_t>(-1); --c) {
    auto &token = (*clang_tokens)[c];
    if(token.is_identifier()) {
      auto &token_offsets = clang_tokens_offsets[c];
      if(line == token_offsets.first.line - 1 && index >= token_offsets.first.index - 1 && index <= token_offsets.second.index - 1) {
        auto referenced = token.get_cursor().get_referenced();
        if(referenced) {
          auto start = get_buffer()->get_iter_at_line_index(token_offsets.first.line - 1, token_offsets.first.index - 1);
          auto end = get_buffer()->get_iter_at_line_index(token_offsets.second.line - 1, token_offsets.second.index - 1);
          get_buffer()->apply_tag(clickable_tag, start, end);
          return;
        }
        break;
      }
    }
  }
  std::smatch sm;
  auto line_at_iter = this->get_line(iter);
  if(std::regex_match(line_at_iter, sm, include_regex)) {
    auto start = get_buffer()->get_iter_at_line(line);
    auto end = start;
    end.forward_to_line_end();
    get_buffer()->apply_tag(clickable_tag, start, end);
  }
}

Source::ClangView::ClangView(const boost::filesystem::path &file_path, const Glib::RefPtr<Gsv::Language> &language)
    : BaseView(file_path, language), ClangViewParse(file_path, language), ClangViewAutocomplete(file_path, language), ClangViewRefactor(file_path, language) {
  do_delete_object.connect([this]() {
    if(delete_thread.joinable())
      delete_thread.join();
    delete this;
  });
}

void Source::ClangView::full_reparse() {
  soft_reparse_needed = false;
  parsed = false;
  delayed_reparse_connection.disconnect();
  delayed_full_reparse_connection.disconnect();

  if(parse_state != ParseState::processing)
    return;

  if(full_reparse_running) {
    delayed_full_reparse_connection = Glib::signal_timeout().connect([this] {
      full_reparse();
      return false;
    }, 100);
    return;
  }

  full_reparse_needed = false;

  parse_process_state = ParseProcessState::idle;
  autocomplete.state = Autocomplete::State::idle;
  auto expected = ParseState::processing;
  if(!parse_state.compare_exchange_strong(expected, ParseState::restarting))
    return;

  full_reparse_running = true;
  if(full_reparse_thread.joinable())
    full_reparse_thread.join();
  full_reparse_thread = std::thread([this]() {
    if(parse_thread.joinable())
      parse_thread.join();
    if(autocomplete.thread.joinable())
      autocomplete.thread.join();
    dispatcher.post([this] {
      parse_initialize();
      full_reparse_running = false;
    });
  });
}

void Source::ClangView::async_delete() {
  delayed_show_arguments_connection.disconnect();

  views.erase(this);
  std::set<boost::filesystem::path> project_paths_in_use;
  for(auto &view : views) {
    if(dynamic_cast<ClangView *>(view)) {
      auto build = Project::Build::create(view->file_path);
      if(!build->project_path.empty())
        project_paths_in_use.emplace(build->project_path);
    }
  }
  Usages::Clang::erase_unused_caches(project_paths_in_use);
  Usages::Clang::cache_in_progress();

  delayed_reparse_connection.disconnect();
  if(full_reparse_needed)
    full_reparse();
  else if(soft_reparse_needed || !parsed)
    soft_reparse();

  auto before_parse_time = std::time(nullptr);
  delete_thread = std::thread([this, before_parse_time, project_paths_in_use = std::move(project_paths_in_use), buffer_modified = get_buffer()->get_modified()] {
    while(!parsed && parse_state != ParseState::stop)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    parse_state = ParseState::stop;

    if(buffer_modified) {
      std::ifstream stream(file_path.string(), std::ios::binary);
      if(stream) {
        std::string buffer;
        buffer.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if(language && (language->get_id() == "chdr" || language->get_id() == "cpphdr"))
          clangmm::remove_include_guard(buffer);
        clang_tu->reparse(buffer);
        clang_tokens = clang_tu->get_tokens();
      }
      else
        clang_tokens = nullptr;
    }

    if(clang_tokens) {
      auto build = Project::Build::create(file_path);
      Usages::Clang::cache(build->project_path, build->get_default_path(), file_path, before_parse_time, project_paths_in_use, clang_tu.get(), clang_tokens.get());
    }
    else
      Usages::Clang::cancel_cache_in_progress();

    if(full_reparse_thread.joinable())
      full_reparse_thread.join();
    if(parse_thread.joinable())
      parse_thread.join();
    if(autocomplete.thread.joinable())
      autocomplete.thread.join();
    do_delete_object();
  });
}
