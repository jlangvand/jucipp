#include "grep.hpp"
#include "config.hpp"
#include "filesystem.hpp"
#include "project_build.hpp"
#include "terminal.hpp"
#include "utility.hpp"

Grep::Grep(const boost::filesystem::path &path, const std::string &pattern, bool case_sensitive, bool extended_regex) {
  auto build = Project::Build::create(path);
  std::string exclude;
  if(!build->project_path.empty()) {
    project_path = build->project_path;
    for(auto &exclude_path : build->get_exclude_paths()) {
#ifdef JUCI_USE_GREP_EXCLUDE
      exclude += " --exclude=" + filesystem::escape_argument(filesystem::get_relative_path(exclude_path, build->project_path).string()) + "/*";
#else
      exclude += " --exclude-dir=" + filesystem::escape_argument(filesystem::get_relative_path(exclude_path, build->project_path).string());
#endif
    }
  }
  else
    project_path = path;

  std::string flags;
  if(!case_sensitive)
    flags += " -i";
  if(extended_regex)
    flags += " -E";

  auto escaped_pattern = '"' + pattern + '"';
  for(size_t i = 1; i < escaped_pattern.size() - 1; ++i) {
    if(escaped_pattern[i] == '"') {
      escaped_pattern.insert(i, "\\");
      ++i;
    }
  }

  std::string command = Config::get().project.grep_command + " -R " + flags + " --color=always --binary-files=without-match " + exclude + " -n " + escaped_pattern + " *";

  std::stringstream stdin_stream;
  Terminal::get().process(stdin_stream, output, command, project_path);
}

Grep::operator bool() {
  output.seekg(0, std::ios::end);
  if(output.tellg() == 0)
    return false;
  output.seekg(0, std::ios::beg);
  return true;
}

Grep::Location Grep::get_location(std::string line, bool color_codes_to_markup, bool include_offset, const std::string &only_for_file) const {
#ifdef _WIN32
  if(!line.empty() && line.back() == '\r')
    line.pop_back();
#endif

  std::vector<std::pair<size_t, size_t>> positions;
  size_t file_end = std::string::npos, line_end = std::string::npos;
  if(color_codes_to_markup) {
    std::string escaped = Glib::Markup::escape_text(line);
    auto decode_escape_sequence = [](const std::string &line, size_t &i) -> bool {
      if(!starts_with(line, i, "&#x1b;["))
        return false;
      i += 7;
      for(; i < line.size(); ++i) {
        if((line[i] >= '0' && line[i] <= '9') || line[i] == ';')
          continue;
        return true;
      }
      return false;
    };
    bool open = false;
    size_t start;
    line.clear();
    line.reserve(escaped.size());
    for(size_t i = 0; i < escaped.size(); ++i) {
      if(decode_escape_sequence(escaped, i)) {
        if(escaped[i] == 'm') {
          if(!open)
            start = line.size();
          else
            positions.emplace_back(start, line.size());
          open = !open;
        }
        continue;
      }
      if(escaped[i] == ':') {
        if(file_end == std::string::npos)
          file_end = line.size();
        else if(line_end == std::string::npos)
          line_end = line.size();
      }
      line += escaped[i];
    }
    if(file_end == std::string::npos || line_end == std::string::npos)
      return {};

    for(auto it = positions.rbegin(); it != positions.rend(); ++it) {
      if(it->first > line_end) {
        line.insert(it->second, "</b>");
        line.insert(it->first, "<b>");
      }
    }
  }
  else {
    file_end = line.find(':');
    if(file_end == std::string::npos)
      return {};
    line_end = line.find(':', file_end + 1);
    if(file_end == std::string::npos)
      return {};
  }

  Location location;
  location.markup = std::move(line);

  auto file = location.markup.substr(0, file_end);
  if(!only_for_file.empty()) {
#ifdef _WIN32
    if(boost::filesystem::path(file) != boost::filesystem::path(only_for_file))
      return location;
#else
    if(file != only_for_file)
      return location;
#endif
  }
  location.file_path = std::move(file);
  try {
    location.line = std::stoul(location.markup.substr(file_end + 1, line_end - file_end)) - 1;
    if(!include_offset) {
      location.offset = 0;
      return location;
    }

    // Find line offset by searching for first match marked with <b></b>
    Glib::ustring ustr = location.markup.substr(line_end + 1);
    size_t offset = 0;
    bool escaped = false;
    for(auto chr : ustr) {
      if(chr == '<')
        break;
      else if(chr == '&')
        escaped = true;
      else if(chr == ';') {
        escaped = false;
        continue;
      }
      else if(escaped)
        continue;
      offset++;
    }
    location.offset = offset;
    return location;
  }
  catch(...) {
    return {};
  }
}
