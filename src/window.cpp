#include "window.hpp"
#include "config.hpp"
#ifdef JUCI_ENABLE_DEBUG
#include "debug_lldb.hpp"
#endif
#include "compile_commands.hpp"
#include "dialogs.hpp"
#include "directories.hpp"
#include "entrybox.hpp"
#include "filesystem.hpp"
#include "grep.hpp"
#include "info.hpp"
#include "menu.hpp"
#include "notebook.hpp"
#include "project.hpp"
#include "selection_dialog.hpp"
#include "terminal.hpp"

Window::Window() {
  Gsv::init();

  set_title("juCi++");
  get_style_context()->add_class("juci_window");
  set_events(Gdk::POINTER_MOTION_MASK | Gdk::FOCUS_CHANGE_MASK | Gdk::SCROLL_MASK | Gdk::LEAVE_NOTIFY_MASK);

  auto visual = get_screen()->get_rgba_visual();
  if(visual)
    gtk_widget_set_visual(reinterpret_cast<GtkWidget *>(gobj()), visual->gobj());

  auto provider = Gtk::CssProvider::create();
  auto screen = get_screen();
  std::string border_radius_style;
  if(screen->get_rgba_visual())
    border_radius_style = "border-radius: 5px; ";
#if GTK_VERSION_GE(3, 20)
  std::string notebook_style(".juci_notebook tab {border-radius: 5px 5px 0 0; padding: 0 4px; margin: 0;}");
#else
  std::string notebook_style(".juci_notebook {-GtkNotebook-tab-overlap: 0px;} .juci_notebook tab {border-radius: 5px 5px 0 0; padding: 4px 4px;}");
#endif
  provider->load_from_data(R"(
    .juci_directories *:selected {border-left-color: inherit; color: inherit; background-color: rgba(128, 128, 128 , 0.2); background-image: inherit;}
    .juci_directories button {background: @theme_base_color; border: 0px; color: @theme_text_color;font-weight: bold;}
    )" + notebook_style + R"(
    .juci_entry {padding: 3px;}
    .juci_terminal_scrolledwindow {padding-left: 3px;}
    .juci_info {border-radius: 5px;}
    .juci_tooltip_window {background-color: transparent;}
    .juci_tooltip_box {)" + border_radius_style + R"(padding: 3px;}
  )");
  get_style_context()->add_provider_for_screen(screen, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  set_menu_actions();
  configure();
  Menu::get().toggle_menu_items();

  Menu::get().right_click_line_menu->attach_to_widget(*this);
  Menu::get().right_click_selected_menu->attach_to_widget(*this);

  EntryBox::get().signal_hide().connect([this]() {
    if(focused_view)
      focused_view->grab_focus();
  });

  Notebook::get().on_focus_page = [this](Source::View *view) {
    if(focused_view != view) {
      focused_view = view;
      update_search_and_replace_entry();
    }
  };
  Notebook::get().on_change_page = [](Source::View *view) {
    Menu::get().toggle_menu_items();

    Directories::get().select(view->file_path);

    if(view->full_reparse_needed)
      view->full_reparse();
    else if(view->soft_reparse_needed)
      view->soft_reparse();

    Notebook::get().update_status(view);

#ifdef JUCI_ENABLE_DEBUG
    if(Project::debugging)
      Project::debug_update_stop();
#endif
  };
  Notebook::get().on_close_page = [this](Source::View *view) {
#ifdef JUCI_ENABLE_DEBUG
    if(Project::current && Project::debugging) {
      auto iter = view->get_buffer()->begin();
      while(view->get_source_buffer()->forward_iter_to_source_mark(iter, "debug_breakpoint") ||
            view->get_source_buffer()->get_source_marks_at_iter(iter, "debug_breakpoint").size()) {
        auto end_iter = view->get_iter_at_line_end(iter.get_line());
        view->get_source_buffer()->remove_source_marks(iter, end_iter, "debug_breakpoint");
        Project::current->debug_remove_breakpoint(view->file_path, iter.get_line() + 1, view->get_buffer()->get_line_count() + 1);
      }
    }
#endif
    EntryBox::get().hide();
    if(auto view = Notebook::get().get_current_view())
      Notebook::get().update_status(view);
    else {
      Notebook::get().clear_status();
      Menu::get().toggle_menu_items();
      if(dynamic_cast<Source::View *>(focused_view))
        focused_view = nullptr;
    }
  };

  Terminal::get().signal_focus_in_event().connect([this](GdkEventFocus *) {
    if(focused_view != &Terminal::get()) {
      focused_view = &Terminal::get();
      update_search_and_replace_entry();
    }
    return false;
  });

  signal_focus_out_event().connect([](GdkEventFocus *event) {
    if(auto view = Notebook::get().get_current_view()) {
      view->hide_tooltips();
      view->hide_dialogs();
    }
    return false;
  });

  signal_delete_event().connect([this](GdkEventAny *) {
    if(!Source::View::non_deleted_views.empty()) {
      hide();
      while(!Source::View::non_deleted_views.empty()) {
        while(Gtk::Main::events_pending())
          Gtk::Main::iteration();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    // TODO 2022 (after Debian Stretch LTS has ended, see issue #354): remove:
    Project::current = nullptr;

    return false;
  });

  Gtk::Settings::get_default()->connect_property_changed("gtk-theme-name", [] {
    Directories::get().update();
    if(auto view = Notebook::get().get_current_view())
      Notebook::get().update_status(view);
  });

  about.signal_response().connect([this](int d) {
    about.hide();
  });

  about.set_logo_icon_name("juci");
  about.set_version(Config::get().version);
  about.set_authors({"(in order of appearance)",
                     "Ted Johan Kristoffersen",
                     "Jørgen Lien Sellæg",
                     "Geir Morten Larsen",
                     "Ole Christian Eidheim"});
  about.set_name("About juCi++");
  about.set_program_name("juCi++");
  about.set_comments("This is an open source IDE with high-end features to make your programming experience juicy");
  about.set_license_type(Gtk::License::LICENSE_MIT_X11);
  about.set_transient_for(*this);
} // Window constructor

void Window::configure() {
  Config::get().load();
  Snippets::get().load();
  auto screen = get_screen();

  static Glib::RefPtr<Gtk::CssProvider> css_provider_theme;
  if(css_provider_theme)
    Gtk::StyleContext::remove_provider_for_screen(screen, css_provider_theme);
  if(Config::get().theme.name.empty()) {
    css_provider_theme = Gtk::CssProvider::create();
    Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme() = (Config::get().theme.variant == "dark");
  }
  else
    css_provider_theme = Gtk::CssProvider::get_named(Config::get().theme.name, Config::get().theme.variant);
  //TODO: add check if theme exists, or else write error to terminal
  Gtk::StyleContext::add_provider_for_screen(screen, css_provider_theme, GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);

  static Glib::RefPtr<Gtk::CssProvider> css_provider_fonts;
  if(css_provider_fonts)
    Gtk::StyleContext::remove_provider_for_screen(screen, css_provider_fonts);
  else
    css_provider_fonts = Gtk::CssProvider::create();

  auto font_description_to_style = [](const Pango::FontDescription &font_description) {
    auto family = font_description.get_family();
    auto size = std::to_string(font_description.get_size() / 1024);
    if(size == "0")
      size.clear();
    if(!family.empty())
      family = "font-family: " + family + ';';
    if(!size.empty())
      size = "font-size: " + size + "px;";
    return family + size;
  };
  auto font_description_string_to_style = [&font_description_to_style](const std::string font_description_string) {
    return font_description_to_style(Pango::FontDescription(font_description_string));
  };

  std::string fonts_style;
  if(!Config::get().theme.font.empty())
    fonts_style += "* {" + font_description_string_to_style(Config::get().theme.font) + "}";
  if(!Config::get().source.font.empty()) {
    auto font_description = Pango::FontDescription(Config::get().source.font);
    fonts_style += ".juci_source_view {" + font_description_to_style(font_description) + "}";
    font_description.set_size(Config::get().source.map_font_size * 1024);
    fonts_style += ".juci_source_map {" + font_description_to_style(font_description) + "}";
  }
  else
    fonts_style += ".juci_source_map {" + font_description_string_to_style(std::to_string(Config::get().source.map_font_size)) + "}";
  if(!Config::get().terminal.font.empty())
    fonts_style += ".juci_terminal {" + font_description_string_to_style(Config::get().terminal.font) + "}";
  else {
    Pango::FontDescription font_description(Config::get().source.font);
    auto font_description_size = font_description.get_size();
    if(font_description_size > 0) {
      font_description.set_size(font_description_size * 0.95);
      fonts_style += ".juci_terminal {" + font_description_to_style(font_description) + "}";
    }
    else {
      auto family = font_description.get_family();
      if(!family.empty())
        family = "font-family: " + family + ';';
      fonts_style += ".juci_terminal {" + family + "font-size: 95%;}";
    }
  }

  if(!fonts_style.empty()) {
    try {
      css_provider_fonts->load_from_data(fonts_style);
      get_style_context()->add_provider_for_screen(screen, css_provider_fonts, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    catch(const Gtk::CssProviderError &e) {
      Terminal::get().print("\e[31mError\e[m: could not override fonts: " + e.what() + '\n', true);
    }
  }

  static Glib::RefPtr<Gtk::CssProvider> css_provider_tooltips;
  if(css_provider_tooltips)
    Gtk::StyleContext::remove_provider_for_screen(screen, css_provider_tooltips);
  else
    css_provider_tooltips = Gtk::CssProvider::create();
  Glib::RefPtr<Gsv::Style> style;
  if(Config::get().source.style.size() > 0) {
    auto scheme = Source::StyleSchemeManager::get_default()->get_scheme(Config::get().source.style);
    if(scheme)
      style = scheme->get_style("def:note");
    else {
      Terminal::get().print("\e[31mError\e[m: Could not find gtksourceview style: " + Config::get().source.style + '\n', true);
    }
  }
  auto foreground_value = style && style->property_foreground_set() ? style->property_foreground().get_value() : get_style_context()->get_color().to_string();
  auto background_value = style && style->property_background_set() ? style->property_background().get_value() : get_style_context()->get_background_color().to_string();
#if GTK_VERSION_GE(3, 20)
  css_provider_tooltips->load_from_data(".juci_tooltip_box {background-color: " + background_value + ";}" +
                                        ".juci_tooltip_text_view text {color: " + foreground_value + ";background-color: " + background_value + ";}");
#else
  css_provider_tooltips->load_from_data(".juci_tooltip_box {background-color: " + background_value + ";}" +
                                        ".juci_tooltip_text_view *:not(:selected) {color: " + foreground_value + ";background-color: " + background_value + ";}");
#endif
  get_style_context()->add_provider_for_screen(screen, css_provider_tooltips, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  Menu::get().set_keys();
  Terminal::get().configure();
  Directories::get().update();
  if(auto view = Notebook::get().get_current_view())
    Notebook::get().update_status(view);
}

void Window::set_menu_actions() {
  auto &menu = Menu::get();

  menu.add_action("about", [this]() {
    about.show();
    about.present();
  });
  menu.add_action("preferences", []() {
    Notebook::get().open(Config::get().home_juci_path / "config" / "config.json");
  });
  menu.add_action("snippets", []() {
    Notebook::get().open(Config::get().home_juci_path / "snippets.json");
  });
  menu.add_action("quit", [this]() {
    close();
  });

  menu.add_action("file_new_file", []() {
    boost::filesystem::path path = Dialog::new_file(Project::get_preferably_view_folder());
    if(!path.empty()) {
      boost::system::error_code ec;
      if(boost::filesystem::exists(path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + path.string() + " already exists.\n", true);
      }
      else {
        if(filesystem::write(path)) {
          if(!Directories::get().path.empty())
            Directories::get().update();
          Notebook::get().open(path);
          if(!Directories::get().path.empty())
            Directories::get().on_save_file(path);
          Terminal::get().print("New file " + path.string() + " created.\n");
        }
        else
          Terminal::get().print("\e[31mError\e[m: could not create new file " + path.string() + ".\n", true);
      }
    }
  });
  menu.add_action("file_new_folder", []() {
    auto time_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    boost::filesystem::path path = Dialog::new_folder(Project::get_preferably_directory_folder());
    boost::system::error_code ec;
    if(!path.empty() && boost::filesystem::exists(path, ec)) {
      auto last_write_time = boost::filesystem::last_write_time(path, ec);
      if(!ec && last_write_time >= time_now) {
        if(!Directories::get().path.empty())
          Directories::get().update();
        else
          Directories::get().open(path);
        Terminal::get().print("New folder " + path.string() + " created.\n");
      }
      else
        Terminal::get().print("\e[31mError\e[m: " + path.string() + " already exists.\n", true);
      Directories::get().select(path);
    }
  });
  menu.add_action("file_new_project_c", []() {
    boost::filesystem::path project_path = Dialog::new_folder(Project::get_preferably_directory_folder());
    if(!project_path.empty()) {
      auto project_name = project_path.filename().string();
      for(auto &chr : project_name) {
        if(chr == ' ')
          chr = '_';
      }
      boost::filesystem::path build_config_path;
      std::string build_config;
      // Depending on default_build_management_system, generate build configuration
      if(Config::get().project.default_build_management_system == "cmake") {
        build_config_path = project_path / "CMakeLists.txt";
        build_config = "cmake_minimum_required(VERSION 2.8)\n\nproject(" + project_name + ")\n\nset(CMAKE_C_FLAGS \"${CMAKE_C_FLAGS} -std=c11 -Wall -Wextra\")\n\nadd_executable(" + project_name + " main.c)\n";
      }
      else if(Config::get().project.default_build_management_system == "meson") {
        build_config_path = project_path / "meson.build";
        build_config = "project('" + project_name + "', 'c')\n\nadd_project_arguments('-std=c11', '-Wall', '-Wextra', language: 'c')\n\nexecutable('" + project_name + "', 'main.c')\n";
      }
      else {
        Terminal::get().print("\e[31mError\e[m: build management system " + Config::get().project.default_build_management_system + " not supported.\n", true);
        return;
      }
      auto c_main_path = project_path / "main.c";
      auto clang_format_path = project_path / ".clang-format";
      boost::system::error_code ec;
      if(boost::filesystem::exists(build_config_path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + build_config_path.string() + " already exists.\n", true);
        return;
      }
      if(boost::filesystem::exists(c_main_path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + c_main_path.string() + " already exists.\n", true);
        return;
      }
      if(boost::filesystem::exists(clang_format_path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + clang_format_path.string() + " already exists.\n", true);
        return;
      }
      std::string c_main = "#include <stdio.h>\n\nint main() {\n  printf(\"Hello World!\\n\");\n}\n";
      std::string clang_format = "IndentWidth: 2\nAccessModifierOffset: -2\nUseTab: Never\nColumnLimit: 0\n";
      if(filesystem::write(build_config_path, build_config) && filesystem::write(c_main_path, c_main) && filesystem::write(clang_format_path, clang_format)) {
        Directories::get().open(project_path);
        Notebook::get().open(c_main_path);
        Directories::get().update();
        Terminal::get().print("C project " + project_name + " created.\n");
      }
      else
        Terminal::get().print("\e[31mError\e[m: Could not create project " + project_path.string() + "\n", true);
    }
  });
  menu.add_action("file_new_project_cpp", []() {
    boost::filesystem::path project_path = Dialog::new_folder(Project::get_preferably_directory_folder());
    if(!project_path.empty()) {
      auto project_name = project_path.filename().string();
      for(auto &chr : project_name) {
        if(chr == ' ')
          chr = '_';
      }
      boost::filesystem::path build_config_path;
      std::string build_config;
      // Depending on default_build_management_system, generate build configuration
      if(Config::get().project.default_build_management_system == "cmake") {
        build_config_path = project_path / "CMakeLists.txt";
        build_config = "cmake_minimum_required(VERSION 2.8)\n\nproject(" + project_name + ")\n\nset(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -std=c++1y -Wall -Wextra\")\n\nadd_executable(" + project_name + " main.cpp)\n";
      }
      else if(Config::get().project.default_build_management_system == "meson") {
        build_config_path = project_path / "meson.build";
        build_config = "project('" + project_name + "', 'cpp')\n\nadd_project_arguments('-std=c++1y', '-Wall', '-Wextra', language: 'cpp')\n\nexecutable('" + project_name + "', 'main.cpp')\n";
      }
      else {
        Terminal::get().print("\e[31mError\e[m: build management system " + Config::get().project.default_build_management_system + " not supported.\n", true);
        return;
      }
      auto cpp_main_path = project_path / "main.cpp";
      auto clang_format_path = project_path / ".clang-format";
      boost::system::error_code ec;
      if(boost::filesystem::exists(build_config_path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + build_config_path.string() + " already exists.\n", true);
        return;
      }
      if(boost::filesystem::exists(cpp_main_path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + cpp_main_path.string() + " already exists.\n", true);
        return;
      }
      if(boost::filesystem::exists(clang_format_path, ec)) {
        Terminal::get().print("\e[31mError\e[m: " + clang_format_path.string() + " already exists.\n", true);
        return;
      }
      std::string cpp_main = "#include <iostream>\n\nint main() {\n  std::cout << \"Hello World!\\n\";\n}\n";
      std::string clang_format = "IndentWidth: 2\nAccessModifierOffset: -2\nUseTab: Never\nColumnLimit: 0\nNamespaceIndentation: All\n";
      if(filesystem::write(build_config_path, build_config) && filesystem::write(cpp_main_path, cpp_main) && filesystem::write(clang_format_path, clang_format)) {
        Directories::get().open(project_path);
        Notebook::get().open(cpp_main_path);
        Directories::get().update();
        Terminal::get().print("C++ project " + project_name + " created.\n");
      }
      else
        Terminal::get().print("\e[31mError\e[m: Could not create project " + project_path.string() + "\n", true);
    }
  });

  menu.add_action("file_open_file", []() {
    auto path = Dialog::open_file(Project::get_preferably_view_folder());
    if(!path.empty())
      Notebook::get().open(path);
  });
  menu.add_action("file_open_folder", []() {
    auto path = Dialog::open_folder(Project::get_preferably_directory_folder());
    if(!path.empty()) {
      Directories::get().open(path);
      if(auto view = Notebook::get().get_current_view())
        Directories::get().select(view->file_path);
    }
  });

  menu.add_action("file_reload_file", []() {
    if(auto view = Notebook::get().get_current_view()) {
      boost::system::error_code ec;
      if(boost::filesystem::exists(view->file_path, ec)) {
        std::ifstream can_read(view->file_path.string());
        if(!can_read) {
          Terminal::get().print("\e[31mError\e[m: could not read " + view->file_path.string() + "\n", true);
          return;
        }
        can_read.close();
      }
      else {
        Terminal::get().print("\e[31mError\e[m: " + view->file_path.string() + " does not exist\n", true);
        return;
      }

      if(view->load())
        view->full_reparse();
    }
  });

  menu.add_action("file_save", [this]() {
    if(auto view = Notebook::get().get_current_view()) {
      if(Notebook::get().save_current()) {
        if(view->file_path == Config::get().home_juci_path / "config" / "config.json") {
          configure();
          for(size_t c = 0; c < Notebook::get().size(); c++) {
            Notebook::get().get_view(c)->configure();
            Notebook::get().configure(c);
          }
        }
      }
    }
  });
  menu.add_action("file_save_as", []() {
    if(auto view = Notebook::get().get_current_view()) {
      auto path = Dialog::save_file_as(view->file_path);
      if(!path.empty()) {
        std::ofstream file(path, std::ofstream::binary);
        if(file) {
          file << view->get_buffer()->get_text().raw();
          file.close();
          if(!Directories::get().path.empty())
            Directories::get().update();
          Notebook::get().open(path);
          Terminal::get().print("File saved to: " + filesystem::get_short_path(filesystem::get_normal_path(path)).string() + "\n");
        }
        else
          Terminal::get().print("Error saving file\n", true);
      }
    }
  });

  menu.add_action("file_close_file", []() {
    if(Notebook::get().get_current_view())
      Notebook::get().close_current();
  });
  menu.add_action("file_close_folder", []() {
    Directories::get().close(Directories::get().path);
  });
  menu.add_action("file_close_project", []() {
    if(!Notebook::get().get_current_view() && Directories::get().path.empty())
      return;
    auto project_path = Project::get_preferably_view_folder();
    auto build = Project::Build::create(project_path);
    if(!build->project_path.empty())
      project_path = build->project_path;
    for(size_t c = Notebook::get().size() - 1; c != static_cast<size_t>(-1); --c) {
      if(filesystem::file_in_path(Notebook::get().get_view(c)->file_path, project_path)) {
        if(!Notebook::get().close(c))
          return;
      }
    }
    Directories::get().close(project_path);
  });

  menu.add_action("file_print", [this]() {
    if(auto view = Notebook::get().get_current_view()) {
      auto print_operation = Gtk::PrintOperation::create();
      auto print_compositor = Gsv::PrintCompositor::create(*view);

      print_operation->set_job_name(view->file_path.filename().string());
      print_compositor->set_wrap_mode(Gtk::WrapMode::WRAP_WORD_CHAR);

      print_operation->signal_begin_print().connect([print_operation, print_compositor](const Glib::RefPtr<Gtk::PrintContext> &print_context) {
        while(!print_compositor->paginate(print_context))
          ;
        print_operation->set_n_pages(print_compositor->get_n_pages());
      });
      print_operation->signal_draw_page().connect([print_compositor](const Glib::RefPtr<Gtk::PrintContext> &print_context, int page_nr) {
        print_compositor->draw_page(print_context, page_nr);
      });

      print_operation->run(Gtk::PRINT_OPERATION_ACTION_PRINT_DIALOG, *this);
    }
  });

  menu.add_action("edit_undo", []() {
    if(auto view = Notebook::get().get_current_view()) {
      auto undo_manager = view->get_source_buffer()->get_undo_manager();
      if(undo_manager->can_undo()) {
        view->disable_spellcheck = true;
        view->enable_multiple_cursors_placements = true;
        undo_manager->undo();
        view->disable_spellcheck = false;
        view->enable_multiple_cursors_placements = false;
        view->hide_tooltips();
        view->scroll_to(view->get_buffer()->get_insert());
      }
    }
  });
  menu.add_action("edit_redo", []() {
    if(auto view = Notebook::get().get_current_view()) {
      auto undo_manager = view->get_source_buffer()->get_undo_manager();
      if(undo_manager->can_redo()) {
        view->disable_spellcheck = true;
        view->enable_multiple_cursors_placements = true;
        undo_manager->redo();
        view->disable_spellcheck = false;
        view->enable_multiple_cursors_placements = false;
        view->hide_tooltips();
        view->scroll_to(view->get_buffer()->get_insert());
      }
    }
  });

  menu.add_action("edit_cut", [this]() {
    // Return if a shown tooltip has selected text
    for(auto tooltip : Tooltips::shown_tooltips) {
      auto buffer = tooltip->buffer;
      if(buffer && buffer->get_has_selection())
        return;
    }

    auto widget = get_focus();
    if(auto entry = dynamic_cast<Gtk::Entry *>(widget)) {
      int start, end;
      if(!entry->get_selection_bounds(start, end)) {
        Gtk::Clipboard::get()->set_text(entry->get_text());
        entry->set_text("");
      }
      else
        entry->cut_clipboard();
    }
    else if(auto view = dynamic_cast<Gtk::TextView *>(widget)) {
      if(!view->get_editable())
        return;
      if(auto source_view = dynamic_cast<Source::View *>(view)) {
        source_view->disable_spellcheck = true;
        source_view->cut();
        source_view->disable_spellcheck = false;
      }
    }
  });
  menu.add_action("edit_cut_lines", [this]() {
    // Return if a shown tooltip has selected text
    for(auto tooltip : Tooltips::shown_tooltips) {
      auto buffer = tooltip->buffer;
      if(buffer && buffer->get_has_selection())
        return;
    }

    auto widget = get_focus();
    if(auto entry = dynamic_cast<Gtk::Entry *>(widget)) {
      Gtk::Clipboard::get()->set_text(entry->get_text());
      entry->set_text("");
    }
    else if(auto view = dynamic_cast<Gtk::TextView *>(widget)) {
      if(!view->get_editable())
        return;
      if(auto source_view = dynamic_cast<Source::View *>(view)) {
        source_view->disable_spellcheck = true;
        source_view->cut_line();
        source_view->disable_spellcheck = false;
      }
    }
  });
  menu.add_action("edit_copy", [this]() {
    // Copy from a tooltip if it has selected text
    for(auto tooltip : Tooltips::shown_tooltips) {
      auto buffer = tooltip->buffer;
      if(buffer && buffer->get_has_selection()) {
        buffer->copy_clipboard(Gtk::Clipboard::get());
        return;
      }
    }

    auto widget = get_focus();
    if(auto entry = dynamic_cast<Gtk::Entry *>(widget)) {
      int start, end;
      if(!entry->get_selection_bounds(start, end))
        Gtk::Clipboard::get()->set_text(entry->get_text());
      else
        entry->copy_clipboard();
    }
    else if(auto view = dynamic_cast<Gtk::TextView *>(widget)) {
      if(!view->get_buffer()->get_has_selection()) {
        auto start = view->get_buffer()->get_iter_at_line(view->get_buffer()->get_insert()->get_iter().get_line());
        auto end = start;
        if(!end.ends_line())
          end.forward_to_line_end();
        end.forward_char();
        Gtk::Clipboard::get()->set_text(view->get_buffer()->get_text(start, end));
      }
      else
        view->get_buffer()->copy_clipboard(Gtk::Clipboard::get());
    }
  });
  menu.add_action("edit_copy_lines", [this]() {
    // Copy from a tooltip if it has selected text
    for(auto tooltip : Tooltips::shown_tooltips) {
      auto buffer = tooltip->buffer;
      if(buffer && buffer->get_has_selection()) {
        Gtk::TextIter start, end;
        buffer->get_selection_bounds(start, end);
        start = buffer->get_iter_at_line(start.get_line());
        if(!end.ends_line())
          end.forward_to_line_end();
        end.forward_char();
        Gtk::Clipboard::get()->set_text(buffer->get_text(start, end));
        return;
      }
    }

    auto widget = get_focus();
    if(auto entry = dynamic_cast<Gtk::Entry *>(widget))
      Gtk::Clipboard::get()->set_text(entry->get_text());
    else if(auto view = dynamic_cast<Gtk::TextView *>(widget)) {
      Gtk::TextIter start, end;
      view->get_buffer()->get_selection_bounds(start, end);
      start = view->get_buffer()->get_iter_at_line(start.get_line());
      if(!end.ends_line())
        end.forward_to_line_end();
      end.forward_char();
      Gtk::Clipboard::get()->set_text(view->get_buffer()->get_text(start, end));
    }
  });
  menu.add_action("edit_paste", [this]() {
    auto widget = get_focus();
    if(auto entry = dynamic_cast<Gtk::Entry *>(widget))
      entry->paste_clipboard();
    else if(auto view = dynamic_cast<Gtk::TextView *>(widget)) {
      auto source_view = dynamic_cast<Source::View *>(view);
      if(source_view) {
        source_view->disable_spellcheck = true;
        source_view->paste();
        source_view->disable_spellcheck = false;
        source_view->hide_tooltips();
      }
      else if(view->get_editable())
        view->get_buffer()->paste_clipboard(Gtk::Clipboard::get());
    }
  });

  menu.add_action("edit_extend_selection", []() {
    if(auto view = Notebook::get().get_current_view())
      view->extend_selection();
  });

  menu.add_action("edit_shrink_selection", []() {
    if(auto view = Notebook::get().get_current_view())
      view->shrink_selection();
  });

  menu.add_action("edit_show_or_hide", []() {
    if(auto view = Notebook::get().get_current_view())
      view->show_or_hide();
  });

  menu.add_action("edit_find", [this]() {
    search_and_replace_entry();
  });

  menu.add_action("source_spellcheck", []() {
    if(auto view = Notebook::get().get_current_view()) {
      view->remove_spellcheck_errors();
      view->spellcheck();
    }
  });
  menu.add_action("source_spellcheck_clear", []() {
    if(auto view = Notebook::get().get_current_view())
      view->remove_spellcheck_errors();
  });
  menu.add_action("source_spellcheck_next_error", []() {
    if(auto view = Notebook::get().get_current_view())
      view->goto_next_spellcheck_error();
  });

  menu.add_action("source_git_next_diff", []() {
    if(auto view = Notebook::get().get_current_view())
      view->goto_next_diff();
  });
  menu.add_action("source_git_show_diff", []() {
    if(auto view = Notebook::get().get_current_view()) {
      auto diff_details = view->get_diff_details();
      if(diff_details.empty())
        return;
      std::string postfix;
      if(diff_details.size() > 2) {
        size_t pos = diff_details.find("@@", 2);
        if(pos != std::string::npos)
          postfix = diff_details.substr(0, pos + 2);
      }
      if(Notebook::get().open(view->file_path.string() + postfix + ".diff")) {
        auto new_view = Notebook::get().get_current_view();
        if(new_view->get_buffer()->get_text().empty()) {
          new_view->get_source_buffer()->begin_not_undoable_action();
          new_view->get_buffer()->set_text(diff_details);
          new_view->get_source_buffer()->end_not_undoable_action();
          new_view->get_buffer()->set_modified(false);
        }
      }
    }
  });

  menu.add_action("source_indentation_set_buffer_tab", [this]() {
    set_tab_entry();
  });
  menu.add_action("source_indentation_auto_indent_buffer", []() {
    auto view = Notebook::get().get_current_view();
    if(view && view->format_style) {
      view->disable_spellcheck = true;
      view->format_style(true);
      view->disable_spellcheck = false;
      view->hide_tooltips();
    }
  });

  menu.add_action("source_goto_line", [this]() {
    goto_line_entry();
  });
  menu.add_action("source_center_cursor", []() {
    if(auto view = Notebook::get().get_current_view())
      view->scroll_to_cursor_delayed(true, false);
  });
  menu.add_action("source_cursor_history_back", []() {
    auto &current_cursor_location = Notebook::get().current_cursor_location;
    if(current_cursor_location == Notebook::get().cursor_locations.end())
      return;

    if(current_cursor_location->view != Notebook::get().get_current_view()) {
      // Move to current position if current position's view is not current view
      // (for instance, in case one is looking at a new file but has not yet placed the cursor within the file)
      if(!Notebook::get().open(current_cursor_location->view))
        return;
    }
    else {
      if(current_cursor_location == Notebook::get().cursor_locations.begin())
        return;

      --current_cursor_location;
      if(current_cursor_location->view != Notebook::get().get_current_view()) {
        if(!Notebook::get().open(current_cursor_location->view))
          return;
      }
    }
    Notebook::get().disable_next_update_cursor_locations = true;
    current_cursor_location->view->get_buffer()->place_cursor(current_cursor_location->mark->get_iter());
    current_cursor_location->view->scroll_to_cursor_delayed(true, false);
  });
  menu.add_action("source_cursor_history_forward", []() {
    auto &current_cursor_location = Notebook::get().current_cursor_location;
    if(current_cursor_location == Notebook::get().cursor_locations.end())
      return;

    if(current_cursor_location->view != Notebook::get().get_current_view()) {
      // Move to current position if current position's view is not current view
      // (for instance, in case one is looking at a new file but has not yet placed the cursor within the file)
      if(!Notebook::get().open(current_cursor_location->view))
        return;
    }
    else {
      if(std::next(current_cursor_location) == Notebook::get().cursor_locations.end())
        return;

      ++current_cursor_location;
      if(current_cursor_location->view != Notebook::get().get_current_view()) {
        if(!Notebook::get().open(current_cursor_location->view))
          return;
      }
    }
    Notebook::get().disable_next_update_cursor_locations = true;
    current_cursor_location->view->get_buffer()->place_cursor(current_cursor_location->mark->get_iter());
    current_cursor_location->view->scroll_to_cursor_delayed(true, false);
  });

  menu.add_action("source_show_completion", [] {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->non_interactive_completion)
        view->non_interactive_completion();
      else
        g_signal_emit_by_name(view->gobj(), "show-completion");
    }
  });

  menu.add_action("source_find_symbol", []() {
    auto project = Project::create();
    project->show_symbols();
  });

  menu.add_action("source_find_pattern", [this]() {
    EntryBox::get().clear();

    EntryBox::get().entries.emplace_back(last_find_pattern, [this](const std::string &pattern_) {
      auto pattern = pattern_; // Store pattern to safely hide entrybox
      EntryBox::get().hide();
      if(!pattern.empty()) {
        auto grep = std::make_shared<Grep>(Project::get_preferably_view_folder(), pattern, find_pattern_case_sensitive, find_pattern_extended_regex);
        if(!*grep) {
          Info::get().print("Pattern not found");
          EntryBox::get().hide();
          return;
        }

        auto view = Notebook::get().get_current_view();
        if(view)
          SelectionDialog::create(view, true, true);
        else
          SelectionDialog::create(true, true);

        std::string current_path;
        unsigned int current_line = 0;
        if(view) {
          current_path = filesystem::get_relative_path(view->file_path, grep->project_path).string();
          current_line = view->get_buffer()->get_insert()->get_iter().get_line();
        }
        bool cursor_set = false;
        std::string line;
        while(std::getline(grep->output, line)) {
          auto location = grep->get_location(std::move(line), true, false, current_path);
          SelectionDialog::get()->add_row(location.markup);
          if(view && location) {
            if(!cursor_set) {
              SelectionDialog::get()->set_cursor_at_last_row();
              cursor_set = true;
            }
            else {
              if(current_line >= location.line)
                SelectionDialog::get()->set_cursor_at_last_row();
            }
          }
        }
        SelectionDialog::get()->on_select = [grep](unsigned int index, const std::string &text, bool hide_window) {
          auto location = grep->get_location(text, false, true);
          if(Notebook::get().open(grep->project_path / location.file_path)) {
            auto view = Notebook::get().get_current_view();
            view->place_cursor_at_line_offset(location.line, location.offset);
            view->scroll_to_cursor_delayed(true, false);
          }
        };
        SelectionDialog::get()->show();
      }
    });
    auto entry_it = EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Pattern");
    entry_it->signal_changed().connect([this, entry_it]() {
      last_find_pattern = entry_it->get_text();
    });

    EntryBox::get().buttons.emplace_back("Find Pattern", [entry_it]() {
      entry_it->activate();
    });

    EntryBox::get().toggle_buttons.emplace_back("Aa");
    EntryBox::get().toggle_buttons.back().set_tooltip_text("Match Case");
    EntryBox::get().toggle_buttons.back().set_active(find_pattern_case_sensitive);
    EntryBox::get().toggle_buttons.back().on_activate = [this]() {
      find_pattern_case_sensitive = !find_pattern_case_sensitive;
    };

    EntryBox::get().toggle_buttons.emplace_back(".*");
    EntryBox::get().toggle_buttons.back().set_tooltip_text("Use Extended Regex");
    EntryBox::get().toggle_buttons.back().set_active(find_pattern_extended_regex);
    EntryBox::get().toggle_buttons.back().on_activate = [this]() {
      find_pattern_extended_regex = !find_pattern_extended_regex;
    };

    EntryBox::get().show();
  });

  menu.add_action("source_find_file", []() {
    auto view_folder = Project::get_preferably_view_folder();
    auto build = Project::Build::create(view_folder);
    auto exclude_paths = build->get_exclude_paths();
    if(!build->project_path.empty())
      view_folder = build->project_path;

    auto view = Notebook::get().get_current_view();
    if(view)
      SelectionDialog::create(view, true, true);
    else
      SelectionDialog::create(true, true);

    std::unordered_set<std::string> buffer_paths;
    for(auto view : Notebook::get().get_views())
      buffer_paths.emplace(view->file_path.string());

    std::vector<boost::filesystem::path> paths;
    // populate with all files in search_path
    boost::system::error_code ec;
    for(boost::filesystem::recursive_directory_iterator iter(view_folder, ec), end; iter != end; iter++) {
      auto path = iter->path();
      // ignore folders
      if(!boost::filesystem::is_regular_file(path, ec)) {
        if(std::any_of(exclude_paths.begin(), exclude_paths.end(), [&path](const boost::filesystem::path &exclude_path) {
             return path == exclude_path;
           }))
          iter.no_push();
        continue;
      }

      // remove project base path
      auto row_str = filesystem::get_relative_path(path, view_folder).string();
      if(buffer_paths.count(path.string()))
        row_str = "<b>" + row_str + "</b>";
      paths.emplace_back(path);
      SelectionDialog::get()->add_row(row_str);
    }

    if(paths.empty()) {
      Info::get().print("No files found in current project");
      return;
    }

    SelectionDialog::get()->on_select = [paths = std::move(paths)](unsigned int index, const std::string &text, bool hide_window) {
      if(Notebook::get().open(paths[index])) {
        auto view = Notebook::get().get_current_view();
        view->hide_tooltips();
      }
    };

    if(view)
      view->hide_tooltips();
    SelectionDialog::get()->show();
  });

  menu.add_action("source_comments_toggle", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->toggle_comments) {
        view->toggle_comments();
      }
    }
  });
  menu.add_action("source_comments_add_documentation", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_documentation_template) {
        auto documentation_template = view->get_documentation_template();
        auto offset = std::get<0>(documentation_template);
        if(offset) {
          boost::system::error_code ec;
          if(!boost::filesystem::is_regular_file(offset.file_path, ec))
            return;
          if(Notebook::get().open(offset.file_path)) {
            auto view = Notebook::get().get_current_view();
            auto iter = view->get_iter_at_line_pos(offset.line, offset.index);
            view->get_buffer()->insert(iter, std::get<1>(documentation_template));
            iter = view->get_iter_at_line_pos(offset.line, offset.index);
            iter.forward_chars(std::get<2>(documentation_template));
            view->get_buffer()->place_cursor(iter);
            view->scroll_to_cursor_delayed(true, false);
          }
        }
      }
    }
  });
  menu.add_action("source_find_documentation", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_token_data) {
        auto data = view->get_token_data();
        std::string uri;
        if(data.size() == 1)
          uri = data[0];
        else if(data.size() > 1) {
          auto documentation_search = Config::get().source.documentation_searches.find(data[0]);
          if(documentation_search != Config::get().source.documentation_searches.end()) {
            std::string token_query;
            for(size_t c = 1; c < data.size(); c++) {
              if(data[c].size() > 0) {
                if(token_query.size() > 0)
                  token_query += documentation_search->second.separator;
                token_query += data[c];
              }
            }
            if(token_query.size() > 0) {
              std::unordered_map<std::string, std::string>::iterator query;
              if(data[1].size() > 0)
                query = documentation_search->second.queries.find(data[1]);
              else
                query = documentation_search->second.queries.find("@empty");
              if(query == documentation_search->second.queries.end())
                query = documentation_search->second.queries.find("@any");

              if(query != documentation_search->second.queries.end())
                uri = query->second + token_query;
            }
          }
        }
        if(!uri.empty()) {
          Notebook::get().open_uri(uri);
        }
      }
    }
  });

  menu.add_action("source_goto_declaration", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_declaration_location) {
        auto location = view->get_declaration_location();
        if(location) {
          boost::system::error_code ec;
          if(!boost::filesystem::is_regular_file(location.file_path, ec))
            return;
          if(Notebook::get().open(location.file_path)) {
            auto view = Notebook::get().get_current_view();
            auto line = static_cast<int>(location.line);
            auto index = static_cast<int>(location.index);
            view->place_cursor_at_line_pos(line, index);
            view->scroll_to_cursor_delayed(true, false);
          }
        }
      }
    }
  });
  menu.add_action("source_goto_type_declaration", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_type_declaration_location) {
        auto location = view->get_type_declaration_location();
        if(location) {
          boost::system::error_code ec;
          if(!boost::filesystem::is_regular_file(location.file_path, ec))
            return;
          if(Notebook::get().open(location.file_path)) {
            auto view = Notebook::get().get_current_view();
            auto line = static_cast<int>(location.line);
            auto index = static_cast<int>(location.index);
            view->place_cursor_at_line_pos(line, index);
            view->scroll_to_cursor_delayed(true, false);
          }
        }
      }
    }
  });
  auto goto_selected_location = [](Source::View *view, const std::vector<Source::Offset> &locations) {
    if(!locations.empty()) {
      SelectionDialog::create(view, true, true);
      std::vector<Source::Offset> rows;
      auto project_path = Project::Build::create(view->file_path)->project_path;
      if(project_path.empty()) {
        if(!Directories::get().path.empty())
          project_path = Directories::get().path;
        else
          project_path = view->file_path.parent_path();
      }
      for(auto &location : locations) {
        auto path = filesystem::get_relative_path(filesystem::get_normal_path(location.file_path), project_path);
        auto row = path.string() + ":" + std::to_string(location.line + 1);
        rows.emplace_back(location);
        SelectionDialog::get()->add_row(row);
      }

      if(rows.size() == 0)
        return;
      else if(rows.size() == 1) {
        auto location = *rows.begin();
        boost::system::error_code ec;
        if(!boost::filesystem::is_regular_file(location.file_path, ec))
          return;
        if(Notebook::get().open(location.file_path)) {
          auto view = Notebook::get().get_current_view();
          auto line = static_cast<int>(location.line);
          auto index = static_cast<int>(location.index);
          view->place_cursor_at_line_pos(line, index);
          view->scroll_to_cursor_delayed(true, false);
        }
        return;
      }
      SelectionDialog::get()->on_select = [rows = std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
        auto location = rows[index];
        boost::system::error_code ec;
        if(!boost::filesystem::is_regular_file(location.file_path, ec))
          return;
        if(Notebook::get().open(location.file_path)) {
          auto view = Notebook::get().get_current_view();
          view->place_cursor_at_line_pos(location.line, location.index);
          view->scroll_to_cursor_delayed(true, false);
        }
      };
      view->hide_tooltips();
      SelectionDialog::get()->show();
    }
  };
  menu.add_action("source_goto_implementation", [goto_selected_location]() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_implementation_locations)
        goto_selected_location(view, view->get_implementation_locations());
    }
  });
  menu.add_action("source_goto_declaration_or_implementation", [goto_selected_location]() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_declaration_or_implementation_locations)
        goto_selected_location(view, view->get_declaration_or_implementation_locations());
    }
  });

  menu.add_action("source_goto_usage", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_usages) {
        auto usages = view->get_usages();
        if(!usages.empty()) {
          SelectionDialog::create(view, true, true);
          std::vector<Source::Offset> rows;

          auto iter = view->get_buffer()->get_insert()->get_iter();
          for(auto &usage : usages) {
            std::string row;
            bool current_page = true;
            //add file name if usage is not in current page
            if(view->file_path != usage.first.file_path) {
              row = usage.first.file_path.filename().string() + ":";
              current_page = false;
            }
            row += std::to_string(usage.first.line + 1) + ": " + usage.second;
            rows.emplace_back(usage.first);
            SelectionDialog::get()->add_row(row);

            //Set dialog cursor to the last row if the textview cursor is at the same line
            if(current_page &&
               iter.get_line() == static_cast<int>(usage.first.line) && iter.get_line_index() >= static_cast<int>(usage.first.index)) {
              SelectionDialog::get()->set_cursor_at_last_row();
            }
          }

          if(rows.size() == 0)
            return;
          SelectionDialog::get()->on_select = [rows = std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
            auto offset = rows[index];
            boost::system::error_code ec;
            if(!boost::filesystem::is_regular_file(offset.file_path, ec))
              return;
            if(Notebook::get().open(offset.file_path)) {
              auto view = Notebook::get().get_current_view();
              view->place_cursor_at_line_pos(offset.line, offset.index);
              view->scroll_to_cursor_delayed(true, false);
            }
          };
          view->hide_tooltips();
          SelectionDialog::get()->show();
        }
      }
    }
  });
  menu.add_action("source_goto_method", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_methods) {
        auto methods = view->get_methods();
        if(!methods.empty()) {
          SelectionDialog::create(view, true, true);
          std::vector<Source::Offset> rows;
          auto iter = view->get_buffer()->get_insert()->get_iter();
          for(auto &method : methods) {
            rows.emplace_back(method.first);
            SelectionDialog::get()->add_row(method.second);
            if(iter.get_line() >= static_cast<int>(method.first.line))
              SelectionDialog::get()->set_cursor_at_last_row();
          }
          SelectionDialog::get()->on_select = [view, rows = std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
            auto offset = rows[index];
            view->get_buffer()->place_cursor(view->get_iter_at_line_pos(offset.line, offset.index));
            view->scroll_to(view->get_buffer()->get_insert(), 0.0, 1.0, 0.5);
            view->hide_tooltips();
          };
          view->hide_tooltips();
          SelectionDialog::get()->show();
        }
      }
    }
  });
  menu.add_action("source_rename", [this]() {
    rename_token_entry();
  });
  menu.add_action("source_implement_method", []() {
    const static std::string button_text = "Insert Method Implementation";

    if(auto view = Notebook::get().get_current_view()) {
      if(view->get_method) {
        auto iter = view->get_buffer()->get_insert()->get_iter();
        if(!EntryBox::get().buttons.empty() && EntryBox::get().buttons.back().get_label() == button_text &&
           iter.ends_line() && iter.starts_line()) {
          EntryBox::get().buttons.back().activate();
          return;
        }
        auto method = view->get_method();
        if(method.empty())
          return;

        EntryBox::get().clear();
        EntryBox::get().labels.emplace_back();
        EntryBox::get().labels.back().set_text(method);
        EntryBox::get().buttons.emplace_back(button_text, [method = std::move(method)]() {
          if(auto view = Notebook::get().get_current_view()) {
            view->get_buffer()->insert_at_cursor(method);
            EntryBox::get().clear();
          }
        });
        EntryBox::get().show();
      }
    }
  });

  menu.add_action("source_goto_next_diagnostic", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->goto_next_diagnostic) {
        view->goto_next_diagnostic();
      }
    }
  });
  menu.add_action("source_apply_fix_its", []() {
    if(auto current_view = Notebook::get().get_current_view()) {
      if(current_view->get_fix_its) {
        auto fix_its = current_view->get_fix_its();

        std::set<Glib::RefPtr<Gtk::TextBuffer>> buffers;
        std::set<Source::View *> header_views;
        std::list<std::pair<Source::Mark, Source::Mark>> fix_it_marks;
        for(auto &fix_it : fix_its) {
          auto view = current_view;
          if(fix_it.path != current_view->file_path) {
            if(Notebook::get().open(fix_it.path)) {
              view = Notebook::get().get_current_view();
              if(CompileCommands::is_header(view->file_path))
                header_views.emplace(view);
            }
          }
          auto start_iter = view->get_iter_at_line_pos(fix_it.offsets.first.line, fix_it.offsets.first.index);
          auto end_iter = view->get_iter_at_line_pos(fix_it.offsets.second.line, fix_it.offsets.second.index);
          fix_it_marks.emplace_back(start_iter, end_iter);
          buffers.emplace(view->get_buffer());
        }

        for(auto &buffer : buffers)
          buffer->begin_user_action();
        auto fix_it_mark = fix_it_marks.begin();
        for(auto &fix_it : fix_its) {
          auto buffer = fix_it_mark->first->get_buffer();
          if(fix_it.type == Source::FixIt::Type::insert)
            buffer->insert(fix_it_mark->first->get_iter(), fix_it.source);
          else if(fix_it.type == Source::FixIt::Type::replace) {
            buffer->erase(fix_it_mark->first->get_iter(), fix_it_mark->second->get_iter());
            buffer->insert(fix_it_mark->first->get_iter(), fix_it.source);
          }
          else if(fix_it.type == Source::FixIt::Type::erase)
            buffer->erase(fix_it_mark->first->get_iter(), fix_it_mark->second->get_iter());
          ++fix_it_mark;
        }
        for(auto &buffer : buffers)
          buffer->end_user_action();

        for(auto &view : header_views) {
          if(view->save())
            Info::get().print("Saved Fix-Its to header file: " + filesystem::get_short_path(view->file_path).string());
        }
        if(current_view != Notebook::get().get_current_view())
          Notebook::get().open(current_view);
      }
    }
  });

  menu.add_action("project_set_run_arguments", []() {
    auto project = Project::create();
    auto run_arguments = project->get_run_arguments();
    if(run_arguments.second.empty())
      return;

    EntryBox::get().clear();
    EntryBox::get().labels.emplace_back();
    auto label_it = EntryBox::get().labels.begin();
    label_it->update = [label_it](int state, const std::string &message) {
      label_it->set_text("Synopsis: [environment_variable=value]... executable [argument]...\nSet empty to let juCi++ deduce executable.");
    };
    label_it->update(0, "");
    EntryBox::get().entries.emplace_back(run_arguments.second, [run_arguments_first = std::move(run_arguments.first)](const std::string &content) {
      Project::run_arguments[run_arguments_first] = content;
      EntryBox::get().hide();
    }, 50);
    auto entry_it = EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Run Arguments");
    EntryBox::get().buttons.emplace_back("Set Run Arguments", [entry_it]() {
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  menu.add_action("project_compile_and_run", []() {
    if(Project::compiling || Project::debugging) {
      Info::get().print("Compile or debug in progress");
      return;
    }

    Project::current = Project::create();

    if(Config::get().project.save_on_compile_or_run)
      Project::save_files(Project::current->build->project_path);

    Project::current->compile_and_run();
  });
  menu.add_action("project_compile", []() {
    if(Project::compiling || Project::debugging) {
      Info::get().print("Compile or debug in progress");
      return;
    }

    Project::current = Project::create();

    if(Config::get().project.save_on_compile_or_run)
      Project::save_files(Project::current->build->project_path);

    Project::current->compile();
  });
  menu.add_action("project_recreate_build", []() {
    if(Project::compiling || Project::debugging) {
      Info::get().print("Compile or debug in progress");
      return;
    }

    Project::current = Project::create();

    Project::current->recreate_build();
  });

  menu.add_action("project_run_command", [this]() {
    EntryBox::get().clear();
    EntryBox::get().labels.emplace_back();
    auto label_it = EntryBox::get().labels.begin();
    label_it->update = [label_it](int state, const std::string &message) {
      label_it->set_text("Run Command directory order: opened directory, file path, current directory");
    };
    label_it->update(0, "");
    EntryBox::get().entries.emplace_back(last_run_command, [this](const std::string &content) {
      if(!content.empty()) {
        last_run_command = content;
        auto directory_folder = Project::get_preferably_directory_folder();
        if(Config::get().terminal.clear_on_run_command)
          Terminal::get().clear();
        Terminal::get().async_print("Running: " + content + '\n');

        Terminal::get().async_process(content, directory_folder, [content](int exit_status) {
          Terminal::get().async_print(content + " returned: " + (exit_status == 0 ? "\e[32m" : "\e[31m") + std::to_string(exit_status) + "\e[m\n");
        });
      }
      EntryBox::get().hide();
    }, 30);
    auto entry_it = EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Command");
    EntryBox::get().buttons.emplace_back("Run Command", [entry_it]() {
      entry_it->activate();
    });
    EntryBox::get().show();
  });

  menu.add_action("project_kill_last_running", []() {
    Terminal::get().kill_last_async_process();
  });
  menu.add_action("project_force_kill_last_running", []() {
    Terminal::get().kill_last_async_process(true);
  });

#ifdef JUCI_ENABLE_DEBUG
  menu.add_action("debug_set_run_arguments", []() {
    auto project = Project::create();
    auto run_arguments = project->debug_get_run_arguments();
    if(run_arguments.second.empty())
      return;

    EntryBox::get().clear();
    EntryBox::get().labels.emplace_back();
    auto label_it = EntryBox::get().labels.begin();
    label_it->update = [label_it](int state, const std::string &message) {
      label_it->set_text("Synopsis: [environment_variable=value]... executable [argument]...\nSet empty to let juCi++ deduce executable.");
    };
    label_it->update(0, "");
    EntryBox::get().entries.emplace_back(run_arguments.second, [run_arguments_first = std::move(run_arguments.first)](const std::string &content) {
      Project::debug_run_arguments[run_arguments_first].arguments = content;
      EntryBox::get().hide();
    }, 50);
    auto entry_it = EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Debug Run Arguments");

    if(auto options = project->debug_get_options()) {
      EntryBox::get().buttons.emplace_back("", [options]() {
        options->show_all();
      });
      EntryBox::get().buttons.back().set_image_from_icon_name("preferences-system");
      EntryBox::get().buttons.back().set_always_show_image(true);
      EntryBox::get().buttons.back().set_tooltip_text("Additional Options");
      options->set_relative_to(EntryBox::get().buttons.back());
    }

    EntryBox::get().buttons.emplace_back("Set Debug Run Arguments", [entry_it]() {
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  menu.add_action("debug_start_continue", []() {
    if(Project::compiling) {
      Info::get().print("Compile in progress");
      return;
    }
    else if(Project::debugging) {
      Project::current->debug_continue();
      return;
    }

    Project::current = Project::create();

    if(Config::get().project.save_on_compile_or_run)
      Project::save_files(Project::current->build->project_path);

    Project::current->debug_start();
  });
  menu.add_action("debug_stop", []() {
    if(Project::current)
      Project::current->debug_stop();
  });
  menu.add_action("debug_kill", []() {
    if(Project::current)
      Project::current->debug_kill();
  });
  menu.add_action("debug_step_over", []() {
    if(Project::current)
      Project::current->debug_step_over();
  });
  menu.add_action("debug_step_into", []() {
    if(Project::current)
      Project::current->debug_step_into();
  });
  menu.add_action("debug_step_out", []() {
    if(Project::current)
      Project::current->debug_step_out();
  });
  menu.add_action("debug_backtrace", []() {
    if(Project::current)
      Project::current->debug_backtrace();
  });
  menu.add_action("debug_show_variables", []() {
    if(Project::current)
      Project::current->debug_show_variables();
  });
  menu.add_action("debug_run_command", [this]() {
    EntryBox::get().clear();
    EntryBox::get().entries.emplace_back(last_run_debug_command, [this](const std::string &content) {
      if(!content.empty()) {
        if(Project::current) {
          if(Config::get().terminal.clear_on_run_command)
            Terminal::get().clear();
          Project::current->debug_run_command(content);
        }
        last_run_debug_command = content;
      }
      EntryBox::get().hide();
    }, 30);
    auto entry_it = EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Debug Command");
    EntryBox::get().buttons.emplace_back("Run Debug Command", [entry_it]() {
      entry_it->activate();
    });
    EntryBox::get().show();
  });
  menu.add_action("debug_toggle_breakpoint", []() {
    if(auto view = Notebook::get().get_current_view()) {
      if(view->toggle_breakpoint)
        view->toggle_breakpoint(view->get_buffer()->get_insert()->get_iter().get_line());
    }
  });
  menu.add_action("debug_show_breakpoints", [] {
    auto current_view = Notebook::get().get_current_view();
    if(!current_view) {
      Info::get().print("No breakpoints found");
      return;
    }

    SelectionDialog::create(current_view, true);

    std::vector<Source::Offset> rows;

    // Place breakpoints in current view first
    auto views = Notebook::get().get_views();
    bool insert_current_view = false;
    for(auto it = views.begin(); it != views.end();) {
      if(*it == current_view) {
        it = views.erase(it);
        insert_current_view = true;
      }
      else
        ++it;
    }
    if(insert_current_view)
      views.insert(views.begin(), current_view);

    auto insert_iter = current_view->get_buffer()->get_insert()->get_iter();
    for(auto &view : views) {
      auto iter = view->get_buffer()->begin();

      do {
        if(view->get_source_buffer()->get_source_marks_at_iter(iter, "debug_breakpoint").size()) {
          rows.emplace_back(iter.get_line(), 0, view->file_path);
          std::string row;
          if(view != current_view)
            row += view->file_path.filename().string() + ':';
          auto source = view->get_line(iter);
          int tabs = 0;
          for(auto chr : source) {
            if(chr == ' ' || chr == '\t')
              ++tabs;
            else
              break;
          }
          SelectionDialog::get()->add_row(row + std::to_string(iter.get_line() + 1) + ": " + source.substr(tabs));
          if(view == current_view && insert_iter.get_line() >= iter.get_line())
            SelectionDialog::get()->set_cursor_at_last_row();
        }
      } while(view->get_source_buffer()->forward_iter_to_source_mark(iter, "debug_breakpoint"));
    }

    if(rows.empty()) {
      Info::get().print("No breakpoints found");
      return;
    }

    SelectionDialog::get()->on_select = [rows = std::move(rows)](unsigned int index, const std::string &text, bool hide_window) {
      if(Notebook::get().open(rows[index].file_path)) {
        auto view = Notebook::get().get_current_view();
        view->place_cursor_at_line_pos(rows[index].line, rows[index].index);
        view->scroll_to_cursor_delayed(true, false);
      }
    };

    SelectionDialog::get()->show();
  });
  menu.add_action("debug_goto_stop", []() {
    if(Project::debugging) {
      if(!Project::debug_stop.first.empty()) {
        if(Notebook::get().open(Project::debug_stop.first)) {
          int line = Project::debug_stop.second.first;
          int index = Project::debug_stop.second.second;
          auto view = Notebook::get().get_current_view();
          view->place_cursor_at_line_index(line, index);
          view->scroll_to_cursor_delayed(true, true);
        }
      }
    }
  });

  Project::debug_update_status("");
#endif

  menu.add_action("window_next_tab", []() {
    Notebook::get().next();
  });
  menu.add_action("window_previous_tab", []() {
    Notebook::get().previous();
  });
  menu.add_action("window_goto_tab", []() {
    auto directory_folder = Project::get_preferably_directory_folder();
    auto build = Project::Build::create(directory_folder);
    if(!build->project_path.empty())
      directory_folder = build->project_path;

    auto current_view = Notebook::get().get_current_view();
    if(current_view)
      SelectionDialog::create(current_view, true, true);
    else
      SelectionDialog::create(true, true);

    std::vector<Source::View *> views;
    for(auto view : Notebook::get().get_views()) {
      views.emplace_back(view);
      SelectionDialog::get()->add_row(filesystem::get_relative_path(view->file_path, directory_folder).string());
      if(view == current_view)
        SelectionDialog::get()->set_cursor_at_last_row();
    }

    if(views.empty()) {
      Info::get().print("No tabs open");
      return;
    }

    SelectionDialog::get()->on_select = [views = std::move(views)](unsigned int index, const std::string &text, bool hide_window) {
      if(Notebook::get().open(views[index])) {
        auto view = Notebook::get().get_current_view();
        view->hide_tooltips();
      }
    };

    if(current_view)
      current_view->hide_tooltips();
    SelectionDialog::get()->show();
  });
  menu.add_action("window_toggle_split", [] {
    Notebook::get().toggle_split();
  });
  menu.add_action("window_split_source_buffer", [] {
    auto view = Notebook::get().get_current_view();
    if(!view) {
      Info::get().print("No source buffers found");
      return;
    }

    auto iter = view->get_buffer()->get_insert()->get_iter();

    if(Notebook::get().open(view->file_path, Notebook::Position::split)) {
      auto new_view = Notebook::get().get_current_view();
      new_view->place_cursor_at_line_offset(iter.get_line(), iter.get_line_offset());
      new_view->scroll_to_cursor_delayed(true, false);
    }
  });
  menu.add_action("window_toggle_full_screen", [this] {
    if(this->get_window()->get_state() & Gdk::WindowState::WINDOW_STATE_FULLSCREEN)
      unfullscreen();
    else
      fullscreen();
  });
  menu.add_action("window_toggle_tabs", [] {
    for(auto &notebook : Notebook::get().notebooks)
      notebook.set_show_tabs(!notebook.get_show_tabs());
  });
  menu.add_action("window_toggle_zen_mode", [this] {
    bool not_zen_mode = std::any_of(Notebook::get().notebooks.begin(),
                                    Notebook::get().notebooks.end(),
                                    [](const Gtk::Notebook &notebook) { return notebook.get_show_tabs(); }) ||
                        directories_scrolled_window.is_visible() || terminal_scrolled_window.is_visible() || get_show_menubar();

    for(auto &notebook : Notebook::get().notebooks)
      notebook.set_show_tabs(!not_zen_mode);
    if(not_zen_mode) {
      directories_scrolled_window.hide();
      terminal_scrolled_window.hide();
    }
    else {
      directories_scrolled_window.show();
      terminal_scrolled_window.show();
    }
    set_show_menubar(!not_zen_mode);
  });
  menu.add_action("window_clear_terminal", [] {
    Terminal::get().clear();
  });

  menu.toggle_menu_items = [] {
    auto &menu = Menu::get();
    auto view = Notebook::get().get_current_view();

    menu.actions["file_reload_file"]->set_enabled(view);
    menu.actions["source_spellcheck"]->set_enabled(view);
    menu.actions["source_spellcheck_clear"]->set_enabled(view);
    menu.actions["source_spellcheck_next_error"]->set_enabled(view);
    menu.actions["source_git_next_diff"]->set_enabled(view);
    menu.actions["source_git_show_diff"]->set_enabled(view);
    menu.actions["source_indentation_set_buffer_tab"]->set_enabled(view);
    menu.actions["source_goto_line"]->set_enabled(view);
    menu.actions["source_center_cursor"]->set_enabled(view);

    menu.actions["source_indentation_auto_indent_buffer"]->set_enabled(view && view->format_style);
    menu.actions["source_comments_toggle"]->set_enabled(view && view->toggle_comments);
    menu.actions["source_comments_add_documentation"]->set_enabled(view && view->get_documentation_template);
    menu.actions["source_find_documentation"]->set_enabled(view && view->get_token_data);
    menu.actions["source_goto_declaration"]->set_enabled(view && view->get_declaration_location);
    menu.actions["source_goto_type_declaration"]->set_enabled(view && view->get_type_declaration_location);
    menu.actions["source_goto_implementation"]->set_enabled(view && view->get_implementation_locations);
    menu.actions["source_goto_declaration_or_implementation"]->set_enabled(view && view->get_declaration_or_implementation_locations);
    menu.actions["source_goto_usage"]->set_enabled(view && view->get_usages);
    menu.actions["source_goto_method"]->set_enabled(view && view->get_methods);
    menu.actions["source_rename"]->set_enabled(view && view->rename_similar_tokens);
    menu.actions["source_implement_method"]->set_enabled(view && view->get_method);
    menu.actions["source_goto_next_diagnostic"]->set_enabled(view && view->goto_next_diagnostic);
    menu.actions["source_apply_fix_its"]->set_enabled(view && view->get_fix_its);
#ifdef JUCI_ENABLE_DEBUG
    Project::debug_activate_menu_items();
#endif
  };
}

void Window::add_widgets() {
  directories_scrolled_window.add(Directories::get());

  auto notebook_vbox = Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
  notebook_vbox->pack_start(Notebook::get());
  notebook_vbox->pack_end(EntryBox::get(), Gtk::PACK_SHRINK);

  terminal_scrolled_window.get_style_context()->add_class("juci_terminal_scrolledwindow");
  terminal_scrolled_window.add(Terminal::get());

  int width, height;
  get_default_size(width, height);

  auto notebook_and_terminal_vpaned = Gtk::manage(new Gtk::Paned(Gtk::Orientation::ORIENTATION_VERTICAL));
  notebook_and_terminal_vpaned->set_position(static_cast<int>(0.75 * height));
  notebook_and_terminal_vpaned->pack1(*notebook_vbox, Gtk::SHRINK);
  notebook_and_terminal_vpaned->pack2(terminal_scrolled_window, Gtk::SHRINK);

  auto hpaned = Gtk::manage(new Gtk::Paned());
  hpaned->set_position(static_cast<int>(0.2 * width));
  hpaned->pack1(directories_scrolled_window, Gtk::SHRINK);
  hpaned->pack2(*notebook_and_terminal_vpaned, Gtk::SHRINK);

  auto status_hbox = Gtk::manage(new Gtk::Box());
  status_hbox->get_style_context()->add_class("juci_status_box");
  status_hbox->set_homogeneous(true);
  status_hbox->pack_start(*Gtk::manage(new Gtk::Box()));
  auto status_right_hbox = Gtk::manage(new Gtk::Box());
  status_right_hbox->pack_end(Notebook::get().status_state, Gtk::PACK_SHRINK);
  auto status_right_overlay = Gtk::manage(new Gtk::Overlay());
  status_right_overlay->add(*status_right_hbox);
  status_right_overlay->add_overlay(Notebook::get().status_diagnostics);
  status_hbox->pack_end(*status_right_overlay);

  auto status_overlay = Gtk::manage(new Gtk::Overlay());
  status_overlay->get_style_context()->add_class("juci_status_overlay");
  status_overlay->add(*status_hbox);
  auto status_file_info_hbox = Gtk::manage(new Gtk::Box);
  status_file_info_hbox->pack_start(Notebook::get().status_file_path, Gtk::PACK_SHRINK);
  status_file_info_hbox->pack_start(Notebook::get().status_branch, Gtk::PACK_SHRINK);
  status_file_info_hbox->pack_start(Notebook::get().status_location, Gtk::PACK_SHRINK);
  status_overlay->add_overlay(*status_file_info_hbox);
  status_overlay->add_overlay(Project::debug_status_label());

  auto vbox = Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
  vbox->pack_start(*hpaned);
  vbox->pack_start(*status_overlay, Gtk::PACK_SHRINK);

  auto overlay_vbox = Gtk::manage(new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL));
  auto overlay_hbox = Gtk::manage(new Gtk::Box());
  overlay_vbox->set_hexpand(false);
  overlay_vbox->set_halign(Gtk::Align::ALIGN_START);
  overlay_vbox->pack_start(Info::get(), Gtk::PACK_SHRINK, 20);
  overlay_hbox->set_hexpand(false);
  overlay_hbox->set_halign(Gtk::Align::ALIGN_END);
  overlay_hbox->pack_end(*overlay_vbox, Gtk::PACK_SHRINK, 20);

  auto overlay = Gtk::manage(new Gtk::Overlay());
  overlay->add(*vbox);
  overlay->add_overlay(*overlay_hbox);
  overlay->set_overlay_pass_through(*overlay_hbox, true);
  add(*overlay);

  show_all_children();
  EntryBox::get().hide();
  Info::get().hide();

  auto scrolled_to_bottom = std::make_shared<bool>(true);
  terminal_scrolled_window.signal_edge_reached().connect([scrolled_to_bottom](Gtk::PositionType position) {
    if(position == Gtk::PositionType::POS_BOTTOM)
      *scrolled_to_bottom = true;
  });
  auto last_value = std::make_shared<double>(terminal_scrolled_window.get_vadjustment()->get_value());
  terminal_scrolled_window.get_vadjustment()->signal_value_changed().connect([this, scrolled_to_bottom, last_value] {
    auto adjustment = terminal_scrolled_window.get_vadjustment();
    if(adjustment->get_value() < *last_value)
      *scrolled_to_bottom = false;
    *last_value = adjustment->get_value();
  });
  terminal_scrolled_window.get_vadjustment()->signal_changed().connect([this, scrolled_to_bottom, last_value] {
    auto adjustment = terminal_scrolled_window.get_vadjustment();
    if(adjustment->get_value() == adjustment->get_upper() - adjustment->get_page_size()) // If for instance the terminal has been cleared
      *scrolled_to_bottom = true;
    *last_value = adjustment->get_value();
  });
  Terminal::get().scroll_to_bottom = [this, scrolled_to_bottom] {
    auto adjustment = terminal_scrolled_window.get_vadjustment();
    adjustment->set_value(adjustment->get_upper() - adjustment->get_page_size());
    *scrolled_to_bottom = true;
    Terminal::get().queue_draw();
  };
  Terminal::get().signal_size_allocate().connect([scrolled_to_bottom](Gtk::Allocation & /*allocation*/) {
    if(*scrolled_to_bottom)
      Terminal::get().scroll_to_bottom();
  });

  EntryBox::get().signal_show().connect([hpaned, notebook_and_terminal_vpaned, notebook_vbox]() {
    hpaned->set_focus_chain({notebook_and_terminal_vpaned});
    notebook_and_terminal_vpaned->set_focus_chain({notebook_vbox});
    notebook_vbox->set_focus_chain({&EntryBox::get()});
  });
  EntryBox::get().signal_hide().connect([hpaned, notebook_and_terminal_vpaned, notebook_vbox]() {
    hpaned->unset_focus_chain();
    notebook_and_terminal_vpaned->unset_focus_chain();
    notebook_vbox->unset_focus_chain();
  });
}

bool Window::on_key_press_event(GdkEventKey *event) {
  if(event->keyval == GDK_KEY_Escape) {
    EntryBox::get().hide();
  }
#ifdef __APPLE__ //For Apple's Command-left, right, up, down keys
  else if((event->state & GDK_META_MASK) > 0 && (event->state & GDK_MOD1_MASK) == 0) {
    if(event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left) {
      event->keyval = GDK_KEY_Home;
      event->state = event->state & GDK_SHIFT_MASK;
      event->hardware_keycode = 115;
    }
    else if(event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right) {
      event->keyval = GDK_KEY_End;
      event->state = event->state & GDK_SHIFT_MASK;
      event->hardware_keycode = 119;
    }
    else if(event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up) {
      event->keyval = GDK_KEY_Home;
      event->state = event->state & GDK_SHIFT_MASK;
      event->state += GDK_CONTROL_MASK;
      event->hardware_keycode = 115;
    }
    else if(event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down) {
      event->keyval = GDK_KEY_End;
      event->state = event->state & GDK_SHIFT_MASK;
      event->state += GDK_CONTROL_MASK;
      event->hardware_keycode = 119;
    }
  }
#endif

  if(SelectionDialog::get() && SelectionDialog::get()->is_visible()) {
    if(SelectionDialog::get()->on_key_press(event))
      return true;
  }

  return Gtk::ApplicationWindow::on_key_press_event(event);
}

bool Window::on_delete_event(GdkEventAny *event) {
  save_session();

  for(size_t c = Notebook::get().size() - 1; c != static_cast<size_t>(-1); --c) {
    if(!Notebook::get().close(c))
      return true;
  }
  Terminal::get().kill_async_processes();

#ifdef JUCI_ENABLE_DEBUG
  Debug::LLDB::destroy();
#endif

  return false;
}

void Window::search_and_replace_entry() {
  EntryBox::get().clear();
  EntryBox::get().labels.emplace_back();
  auto label_it = EntryBox::get().labels.begin();
  label_it->update = [label_it](int state, const std::string &message) {
    if(state == 0) {
      try {
        auto number = stoi(message);
        if(number == 0)
          label_it->set_text("");
        else if(number == 1)
          label_it->set_text("1 result found");
        else if(number > 1)
          label_it->set_text(message + " results found");
      }
      catch(const std::exception &e) {
      }
    }
  };

  if(focused_view) {
    if(Config::get().source.search_for_selection) {
      Gtk::TextIter start, end;
      if(focused_view->get_buffer()->get_selection_bounds(start, end))
        last_search = focused_view->get_buffer()->get_text(start, end);
    }
  }
  EntryBox::get().entries.emplace_back(last_search, [this](const std::string &content) {
    if(focused_view)
      focused_view->search_forward();
  });
  auto search_entry_it = EntryBox::get().entries.begin();
  search_entry_it->set_placeholder_text("Find");
  search_entry_it->signal_key_press_event().connect([this](GdkEventKey *event) {
    if((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) && (event->state & GDK_SHIFT_MASK) > 0) {
      if(focused_view)
        focused_view->search_backward();
    }
    return false;
  });
  search_entry_it->signal_changed().connect([this, search_entry_it]() {
    last_search = search_entry_it->get_text();
    if(focused_view)
      focused_view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  });

  EntryBox::get().entries.emplace_back(last_replace, [this](const std::string &content) {
    if(focused_view)
      focused_view->replace_forward(content);
  });
  auto replace_entry_it = EntryBox::get().entries.begin();
  replace_entry_it++;
  replace_entry_it->set_placeholder_text("Replace");
  replace_entry_it->signal_key_press_event().connect([this, replace_entry_it](GdkEventKey *event) {
    if((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) && (event->state & GDK_SHIFT_MASK) > 0) {
      if(focused_view)
        focused_view->replace_backward(replace_entry_it->get_text());
    }
    return false;
  });
  replace_entry_it->signal_changed().connect([this, replace_entry_it]() {
    last_replace = replace_entry_it->get_text();
  });

  EntryBox::get().buttons.emplace_back("↑", [this]() {
    if(focused_view)
      focused_view->search_backward();
  });
  EntryBox::get().buttons.back().set_tooltip_text("Find Previous\n\nShortcut: Shift+Enter in the Find entry field");
  EntryBox::get().buttons.emplace_back("⇄", [this, replace_entry_it]() {
    if(focused_view)
      focused_view->replace_forward(replace_entry_it->get_text());
  });
  EntryBox::get().buttons.back().set_tooltip_text("Replace Next\n\nShortcut: Enter in the Replace entry field");
  EntryBox::get().buttons.emplace_back("↓", [this]() {
    if(focused_view)
      focused_view->search_forward();
  });
  EntryBox::get().buttons.back().set_tooltip_text("Find Next\n\nShortcut: Enter in the Find entry field");
  EntryBox::get().buttons.emplace_back("Replace All", [this, replace_entry_it]() {
    if(focused_view)
      focused_view->replace_all(replace_entry_it->get_text());
  });
  EntryBox::get().buttons.back().set_tooltip_text("Replace All");

  EntryBox::get().toggle_buttons.emplace_back("Aa");
  EntryBox::get().toggle_buttons.back().set_tooltip_text("Match Case");
  EntryBox::get().toggle_buttons.back().set_active(case_sensitive_search);
  EntryBox::get().toggle_buttons.back().on_activate = [this, search_entry_it]() {
    case_sensitive_search = !case_sensitive_search;
    if(focused_view)
      focused_view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  };
  EntryBox::get().toggle_buttons.emplace_back(".*");
  EntryBox::get().toggle_buttons.back().set_tooltip_text("Use Regex");
  EntryBox::get().toggle_buttons.back().set_active(regex_search);
  EntryBox::get().toggle_buttons.back().on_activate = [this, search_entry_it]() {
    regex_search = !regex_search;
    if(focused_view)
      focused_view->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  };

  EntryBox::get().signal_hide().connect([this]() {
    for(size_t c = 0; c < Notebook::get().size(); c++) {
      Notebook::get().get_view(c)->update_search_occurrences = nullptr;
      Notebook::get().get_view(c)->search_highlight("", case_sensitive_search, regex_search);
    }
    Terminal::get().update_search_occurrences = nullptr;
    Terminal::get().search_highlight("", case_sensitive_search, regex_search);
    search_entry_shown = false;
  });

  search_entry_shown = true;
  update_search_and_replace_entry();
  EntryBox::get().show();
}

void Window::update_search_and_replace_entry() {
  if(!search_entry_shown || !focused_view)
    return;

  focused_view->update_search_occurrences = [](int number) {
    EntryBox::get().labels.begin()->update(0, std::to_string(number));
  };
  focused_view->search_highlight(last_search, case_sensitive_search, regex_search);

  auto source_view = dynamic_cast<Source::View *>(focused_view);

  auto entry = EntryBox::get().entries.begin();
  entry++;
  entry->set_sensitive(source_view);

  auto button = EntryBox::get().buttons.begin();
  button++;
  button->set_sensitive(source_view);
  button++;
  button++;
  button->set_sensitive(source_view);
}

void Window::set_tab_entry() {
  EntryBox::get().clear();
  if(auto view = Notebook::get().get_current_view()) {
    auto tab_char_and_size = view->get_tab_char_and_size();

    EntryBox::get().labels.emplace_back();
    auto label_it = EntryBox::get().labels.begin();

    EntryBox::get().entries.emplace_back(std::to_string(tab_char_and_size.second));
    auto entry_tab_size_it = EntryBox::get().entries.begin();
    entry_tab_size_it->set_placeholder_text("Tab Size");

    char tab_char = tab_char_and_size.first;
    std::string tab_char_string;
    if(tab_char == ' ')
      tab_char_string = "space";
    else if(tab_char == '\t')
      tab_char_string = "tab";

    EntryBox::get().entries.emplace_back(tab_char_string);
    auto entry_tab_char_it = EntryBox::get().entries.rbegin();
    entry_tab_char_it->set_placeholder_text("Tab Char");

    const auto activate_function = [entry_tab_char_it, entry_tab_size_it, label_it](const std::string &content) {
      if(auto view = Notebook::get().get_current_view()) {
        char tab_char = 0;
        unsigned tab_size = 0;
        try {
          tab_size = static_cast<unsigned>(std::stoul(entry_tab_size_it->get_text()));
          std::string tab_char_string = entry_tab_char_it->get_text();
          std::transform(tab_char_string.begin(), tab_char_string.end(), tab_char_string.begin(), ::tolower);
          if(tab_char_string == "space")
            tab_char = ' ';
          else if(tab_char_string == "tab")
            tab_char = '\t';
        }
        catch(const std::exception &e) {
        }

        if(tab_char != 0 && tab_size > 0) {
          view->set_tab_char_and_size(tab_char, tab_size);
          EntryBox::get().hide();
        }
        else {
          label_it->set_text("Tab size must be >0 and tab char set to either 'space' or 'tab'");
        }
      }
    };

    entry_tab_char_it->on_activate = activate_function;
    entry_tab_size_it->on_activate = activate_function;

    EntryBox::get().buttons.emplace_back("Set Tab", [entry_tab_char_it]() {
      entry_tab_char_it->activate();
    });

    EntryBox::get().show();
  }
}

void Window::goto_line_entry() {
  EntryBox::get().clear();
  if(Notebook::get().get_current_view()) {
    EntryBox::get().entries.emplace_back("", [](const std::string &content) {
      if(auto view = Notebook::get().get_current_view()) {
        try {
          view->place_cursor_at_line_index(stoi(content) - 1, 0);
          view->scroll_to_cursor_delayed(true, false);
        }
        catch(const std::exception &e) {
        }
        EntryBox::get().hide();
      }
    });
    auto entry_it = EntryBox::get().entries.begin();
    entry_it->set_placeholder_text("Line Number");
    EntryBox::get().buttons.emplace_back("Go to Line", [entry_it]() {
      entry_it->activate();
    });
    EntryBox::get().show();
  }
}

void Window::rename_token_entry() {
  EntryBox::get().clear();
  if(auto view = Notebook::get().get_current_view()) {
    if(view->get_token_spelling && view->rename_similar_tokens) {
      auto spelling = std::make_shared<std::string>(view->get_token_spelling());
      if(!spelling->empty()) {
        EntryBox::get().entries.emplace_back(*spelling, [view, spelling, iter = view->get_buffer()->get_insert()->get_iter()](const std::string &content) {
          //TODO: gtk needs a way to check if iter is valid without dumping g_error message
          //iter->get_buffer() will print such a message, but no segfault will occur
          if(Notebook::get().get_current_view() == view && content != *spelling && iter.get_buffer() && view->get_buffer()->get_insert()->get_iter() == iter)
            view->rename_similar_tokens(content);
          else
            Info::get().print("Operation canceled");
          EntryBox::get().hide();
        });
        auto entry_it = EntryBox::get().entries.begin();
        entry_it->set_placeholder_text("New Name");
        EntryBox::get().buttons.emplace_back("Rename", [entry_it]() {
          entry_it->activate();
        });
        EntryBox::get().show();
      }
    }
  }
}

void Window::save_session() {
  try {
    boost::property_tree::ptree root_pt;
    root_pt.put("folder", Directories::get().path.string());

    boost::property_tree::ptree files_pt;
    for(auto &notebook_view : Notebook::get().get_notebook_views()) {
      boost::property_tree::ptree file_pt;
      file_pt.put("path", notebook_view.second->file_path.string());
      file_pt.put("notebook", notebook_view.first);
      auto iter = notebook_view.second->get_buffer()->get_insert()->get_iter();
      file_pt.put("line", iter.get_line());
      file_pt.put("line_offset", iter.get_line_offset());
      files_pt.push_back(std::make_pair("", file_pt));
    }
    root_pt.add_child("files", files_pt);

    boost::property_tree::ptree current_file_pt;
    if(auto view = Notebook::get().get_current_view()) {
      current_file_pt.put("path", view->file_path.string());
      auto iter = view->get_buffer()->get_insert()->get_iter();
      current_file_pt.put("line", iter.get_line());
      current_file_pt.put("line_offset", iter.get_line_offset());
    }
    std::string current_path;
    if(auto view = Notebook::get().get_current_view())
      current_path = view->file_path.string();
    root_pt.put("current_file", current_path);

    boost::property_tree::ptree run_arguments_pt;
    for(auto &run_argument : Project::run_arguments) {
      if(run_argument.second.empty())
        continue;
      if(boost::filesystem::exists(run_argument.first) && boost::filesystem::is_directory(run_argument.first)) {
        boost::property_tree::ptree run_argument_pt;
        run_argument_pt.put("path", run_argument.first);
        run_argument_pt.put("arguments", run_argument.second);
        run_arguments_pt.push_back(std::make_pair("", run_argument_pt));
      }
    }
    root_pt.add_child("run_arguments", run_arguments_pt);

    boost::property_tree::ptree debug_run_arguments_pt;
    for(auto &debug_run_argument : Project::debug_run_arguments) {
      if(debug_run_argument.second.arguments.empty() && !debug_run_argument.second.remote_enabled && debug_run_argument.second.remote_host_port.empty())
        continue;
      if(boost::filesystem::exists(debug_run_argument.first) && boost::filesystem::is_directory(debug_run_argument.first)) {
        boost::property_tree::ptree debug_run_argument_pt;
        debug_run_argument_pt.put("path", debug_run_argument.first);
        debug_run_argument_pt.put("arguments", debug_run_argument.second.arguments);
        debug_run_argument_pt.put("remote_enabled", debug_run_argument.second.remote_enabled);
        debug_run_argument_pt.put("remote_host_port", debug_run_argument.second.remote_host_port);
        debug_run_arguments_pt.push_back(std::make_pair("", debug_run_argument_pt));
      }
    }
    root_pt.add_child("debug_run_arguments", debug_run_arguments_pt);

    int width, height;
    get_size(width, height);
    boost::property_tree::ptree window_pt;
    window_pt.put("width", width);
    window_pt.put("height", height);
    root_pt.add_child("window", window_pt);

    boost::property_tree::write_json((Config::get().home_juci_path / "last_session.json").string(), root_pt);
  }
  catch(...) {
  }
}

void Window::load_session(std::vector<boost::filesystem::path> &directories, std::vector<std::pair<boost::filesystem::path, size_t>> &files, std::vector<std::pair<int, int>> &file_offsets, std::string &current_file, bool read_directories_and_files) {
  try {
    boost::property_tree::ptree root_pt;
    boost::property_tree::read_json((Config::get().home_juci_path / "last_session.json").string(), root_pt);
    if(read_directories_and_files) {
      auto folder = root_pt.get<std::string>("folder");
      if(!folder.empty() && boost::filesystem::exists(folder) && boost::filesystem::is_directory(folder))
        directories.emplace_back(folder);

      for(auto &file_pt : root_pt.get_child("files")) {
        auto file = file_pt.second.get<std::string>("path");
        auto notebook = file_pt.second.get<size_t>("notebook");
        auto line = file_pt.second.get<int>("line");
        auto line_offset = file_pt.second.get<int>("line_offset");
        if(!file.empty() && boost::filesystem::exists(file) && !boost::filesystem::is_directory(file)) {
          files.emplace_back(file, notebook);
          file_offsets.emplace_back(line, line_offset);
        }
      }

      current_file = root_pt.get<std::string>("current_file");
    }

    for(auto &run_argument : root_pt.get_child(("run_arguments"))) {
      auto path = run_argument.second.get<std::string>("path");
      boost::system::error_code ec;
      if(boost::filesystem::exists(path, ec) && boost::filesystem::is_directory(path, ec))
        Project::run_arguments.emplace(path, run_argument.second.get<std::string>("arguments"));
    }

    for(auto &debug_run_argument : root_pt.get_child(("debug_run_arguments"))) {
      auto path = debug_run_argument.second.get<std::string>("path");
      boost::system::error_code ec;
      if(boost::filesystem::exists(path, ec) && boost::filesystem::is_directory(path, ec))
        Project::debug_run_arguments.emplace(path, Project::DebugRunArguments{debug_run_argument.second.get<std::string>("arguments"),
                                                                              debug_run_argument.second.get<bool>("remote_enabled"),
                                                                              debug_run_argument.second.get<std::string>("remote_host_port")});
    }

    auto window_pt = root_pt.get_child("window");
    set_default_size(window_pt.get<int>("width"), window_pt.get<int>("height"));
  }
  catch(...) {
    set_default_size(800, 600);
  }
}
