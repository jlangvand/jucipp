#include "commands.hpp"
#include "config.hpp"
#include "filesystem.hpp"
#include "terminal.hpp"
#include <boost/property_tree/json_parser.hpp>

void Commands::load() {
  auto commands_file = Config::get().home_juci_path / "commands.json";

  boost::system::error_code ec;
  if(!boost::filesystem::exists(commands_file, ec))
    filesystem::write(commands_file, R"([
  {
    "key": "<primary><shift>1",
    "path_comment": "Regular expression for which paths this command should apply",
    "path": "^.*\\.json$",
    "compile_comment": "Add compile command if a compilation step is needed prior to the run command. <path_match> is set to the matching file or directory, and <working_directory> is set to the project directory if found or the matching file's directory.",
    "compile": "",
    "run_comment": "<path_match> is set to the matching file or directory, and <working_directory> is set to the project directory if found or the matching file's directory",
    "run": "echo <path_match> && echo <working_directory>",
    "debug_comment": "Whether or not this command should run through debugger",
    "debug": false,
    "debug_remote_host": ""
  }
]
)");

  commands.clear();
  try {
    boost::property_tree::ptree pt;
    boost::property_tree::json_parser::read_json(commands_file.string(), pt);
    for(auto command_it = pt.begin(); command_it != pt.end(); ++command_it) {
      auto key_string = command_it->second.get<std::string>("key");
      guint key = 0;
      GdkModifierType modifier = static_cast<GdkModifierType>(0);
      if(!key_string.empty()) {
        gtk_accelerator_parse(key_string.c_str(), &key, &modifier);
        if(key == 0 && modifier == 0)
          Terminal::get().async_print("\e[31mError\e[m: could not parse key string: " + key_string + "\n", true);
      }
      auto path = command_it->second.get<std::string>("path", "");
      boost::optional<std::regex> regex;
      if(!path.empty())
        regex = std::regex(path, std::regex::optimize);
      commands.emplace_back(Command{key, modifier, std::move(regex),
                                    command_it->second.get<std::string>("compile", ""), command_it->second.get<std::string>("run"),
                                    command_it->second.get<bool>("debug", false), command_it->second.get<std::string>("debug_remote_host", "")});
    }
  }
  catch(const std::exception &e) {
    Terminal::get().async_print(std::string("\e[31mError\e[m: ") + e.what() + "\n", true);
  }
}
