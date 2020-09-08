from Jucipp import Config

def menu():
    menu = Config.Menu()
    menu.keys = {
       'key': 'value',
    }
    Config().menu = menu

def theme():
    theme = Config.Theme()
    theme.name = "Star Wars"
    theme.variant = "Instrumental"
    theme.font = "Imperial"
    Config().theme = theme

def terminal():
    terminal = Config.Terminal()
    terminal.font = "Comic Sans"
    terminal.history_size = 3
    Config().terminal = terminal

def project():
    project = Config.Project()
    project.default_build_path = "/build"
    project.debug_build_path = "/debug"
    meson = Config.Project.Meson()
    meson.command = "meson"
    meson.compile_command = "meson --build"
    cmake = Config.Project.CMake()
    cmake.command = "cmake"
    cmake.compile_command = "cmake --build"
    project.meson = meson
    project.cmake = cmake
    project.save_on_compile_or_run = True
    # project.clear_terminal_on_compile = False
    project.ctags_command = "ctags"
    project.python_command = "python"
    Config().project = project

def source():
    source = Config.Source()
    source.style = "Classical"
    source.font = "Monospaced"
    source.spellcheck_language = "Klingon"
    source.cleanup_whitespace_characters = False
    source.show_whitespace_characters = "no"
    source.format_style_on_save = False
    source.format_style_on_save_if_style_file_found = False
    source.smart_inserts = False
    source.show_map = False
    # source.map_font_size = "10px"
    source.show_git_diff = False
    source.show_background_pattern = False
    source.show_right_margin = False
    source.right_margin_position = 10
    source.auto_tab_char_and_size = False
    source.default_tab_char = "c"
    source.default_tab_size = 1
    source.tab_indents_line = False
    # source.wrap_lines = False
    source.highlight_current_line = False
    source.show_line_numbers = False
    source.enable_multiple_cursors = False
    source.auto_reload_changed_files = False
    source.clang_format_style = "CFS"
    source.clang_usages_threads = 1
    documentation_search = Config.Source.DocumentationSearch()
    documentation_search.separator = '::'
    documentation_search.queries = {
        'key': 'value',
    }
    source.documentation_searches = {
        'cpp' : documentation_search
    }
    Config().source = source

def log():
    log = Config.Log()
    log.libclang = True
    log.language_server = False
    Config().log = log

def cfg():
    config = Config()
    config.home_path = "/home"
    config.home_juci_path = "/away"
