#include <boost/property_tree/json_parser.hpp>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif


int main() {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  std::string line;
  try {
    // Read initialize and respond
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "initialize")
        return 1;

      std::string result = R"({
    "jsonrpc": "2.0",
    "id": "0",
    "result": {
        "capabilities": {
            "textDocumentSync": {
                "openClose": "true",
                "change": "2",
                "save": ""
            },
            "selectionRangeProvider": "true",
            "hoverProvider": "true",
            "completionProvider": {
                "triggerCharacters": [
                    ":",
                    ".",
                    "'"
                ]
            },
            "signatureHelpProvider": {
                "triggerCharacters": [
                    "(",
                    ","
                ]
            },
            "definitionProvider": "true",
            "typeDefinitionProvider": "true",
            "implementationProvider": "true",
            "referencesProvider": "true",
            "documentHighlightProvider": "true",
            "documentSymbolProvider": "true",
            "workspaceSymbolProvider": "true",
            "codeActionProvider": {
                "codeActionKinds": [
                    "",
                    "quickfix",
                    "refactor",
                    "refactor.extract",
                    "refactor.inline",
                    "refactor.rewrite"
                ],
                "resolveProvider": "true"
            },
            "codeLensProvider": {
                "resolveProvider": "true"
            },
            "documentFormattingProvider": "true",
            "documentOnTypeFormattingProvider": {
                "firstTriggerCharacter": "=",
                "moreTriggerCharacter": [
                    ".",
                    ">",
                    "{"
                ]
            },
            "renameProvider": {
                "prepareProvider": "true"
            },
            "foldingRangeProvider": "true",
            "workspace": {
                "fileOperations": {
                    "willRename": {
                        "filters": [
                            {
                                "scheme": "file",
                                "pattern": {
                                    "glob": "**\/*.rs",
                                    "matches": "file"
                                }
                            },
                            {
                                "scheme": "file",
                                "pattern": {
                                    "glob": "**",
                                    "matches": "folder"
                                }
                            }
                        ]
                    }
                }
            },
            "callHierarchyProvider": "true",
            "semanticTokensProvider": {
                "legend": {
                    "tokenTypes": [
                        "comment",
                        "keyword",
                        "string",
                        "number",
                        "regexp",
                        "operator",
                        "namespace",
                        "type",
                        "struct",
                        "class",
                        "interface",
                        "enum",
                        "enumMember",
                        "typeParameter",
                        "function",
                        "method",
                        "property",
                        "macro",
                        "variable",
                        "parameter",
                        "angle",
                        "arithmetic",
                        "attribute",
                        "bitwise",
                        "boolean",
                        "brace",
                        "bracket",
                        "builtinType",
                        "characterLiteral",
                        "colon",
                        "comma",
                        "comparison",
                        "constParameter",
                        "dot",
                        "escapeSequence",
                        "formatSpecifier",
                        "generic",
                        "label",
                        "lifetime",
                        "logical",
                        "operator",
                        "parenthesis",
                        "punctuation",
                        "selfKeyword",
                        "semicolon",
                        "typeAlias",
                        "union",
                        "unresolvedReference"
                    ],
                    "tokenModifiers": [
                        "documentation",
                        "declaration",
                        "definition",
                        "static",
                        "abstract",
                        "deprecated",
                        "readonly",
                        "constant",
                        "controlFlow",
                        "injected",
                        "mutable",
                        "consuming",
                        "async",
                        "unsafe",
                        "attribute",
                        "trait",
                        "callable",
                        "intraDocLink"
                    ]
                },
                "range": "true",
                "full": {
                    "delta": "true"
                }
            },
            "experimental": {
                "joinLines": "true",
                "ssr": "true",
                "onEnter": "true",
                "parentModule": "true",
                "runnables": {
                    "kinds": [
                        "cargo"
                    ]
                },
                "workspaceSymbolScopeKindFiltering": "true"
            }
        },
        "serverInfo": {
            "name": "rust-analyzer",
            "version": "3022a2c3a 2021-05-25 dev"
        },
        "offsetEncoding": "utf-8"
    }
})";
      std::cout << "Content-Length: " << result.size() << "\r\n\r\n"
                << result;
    }

    // Read initialized
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "initialized")
        return 2;
    }

    // Read textDocument/didOpen
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "textDocument/didOpen")
        return 3;
    }

    // Read textDocument/didChange
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "textDocument/didChange")
        return 4;
    }

    // Read and write textDocument/formatting
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "textDocument/formatting")
        return 5;

      std::string result = R"({
    "jsonrpc": "2.0",
    "id": "1",
    "result": [
        {
            "range": {
                "start": {
                    "line": "0",
                    "character": "0"
                },
                "end": {
                    "line": "0",
                    "character": "1"
                }
            },
            "newText": ""
        }
    ]
})";
      std::cout << "Content-Length: " << result.size() << "\r\n\r\n"
                << result;
    }

    // Read textDocument/didChange
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "textDocument/didChange")
        return 6;
    }

    // Read textDocument/didClose
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "textDocument/didClose")
        return 7;
    }

    // Read shutdown and respond
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "shutdown")
        return 8;

      std::string result = R"({
    "jsonrpc": "2.0",
    "id": "2",
    "result": {}
})";
      std::cout << "Content-Length: " << result.size() << "\r\n\r\n"
                << result;
    }

    // Read exit
    {
      std::getline(std::cin, line);
      auto size = std::atoi(line.substr(16).c_str());
      std::getline(std::cin, line);
      std::string buffer;
      buffer.resize(size);
      std::cin.read(&buffer[0], size);
      std::stringstream ss(buffer);
      boost::property_tree::ptree pt;
      boost::property_tree::json_parser::read_json(ss, pt);
      if(pt.get<std::string>("method") != "exit")
        return 9;
    }
  }
  catch(const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 100;
  }
}
