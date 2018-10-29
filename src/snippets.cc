#include "snippets.h"
#include "config.h"
#include "filesystem.h"
#include "terminal.h"
#include <boost/property_tree/json_parser.hpp>

Snippets::Snippets() {
  load();
}

void Snippets::load() {
  auto snippets_file = Config::get().home_juci_path / "snippets.json";

  boost::system::error_code ec;
  if(!boost::filesystem::exists(snippets_file, ec))
    filesystem::write(snippets_file, R"({
    "^markdown$": [
      {
        "prefix": "code_block",
        "body": "```${1:language}\n${2:code}\n```\n",
        "description": "Insert code block"
      }
    ]
  }
  )");

  snippets.clear();
  try {
    boost::property_tree::ptree pt;
    boost::property_tree::json_parser::read_json(snippets_file.string(), pt);
    for(auto language_it = pt.begin(); language_it != pt.end(); ++language_it) {
      snippets.emplace_back(std::regex(language_it->first), std::vector<Snippet>());
      for(auto snippet_it = language_it->second.begin(); snippet_it != language_it->second.end(); ++snippet_it)
        snippets.back().second.emplace_back(Snippet{snippet_it->second.get<std::string>("prefix"), snippet_it->second.get<std::string>("body"), snippet_it->second.get<std::string>("description", "")});
    }
  }
  catch(const std::exception &e) {
    Terminal::get().async_print(std::string("Error: ") + e.what() + "\n", true);
  }
}