#include "filesystem.h"
#include "source.h"
#include <glib.h>

std::string hello_world = R"(#include <iostream>  
    
int main() {  
  std::cout << "hello world\n";    
})";

std::string hello_world_cleaned = R"(#include <iostream>

int main() {
  std::cout << "hello world\n";
}
)";

//Requires display server to work
//However, it is possible to use the Broadway backend if the test is run in a pure terminal environment:
//broadwayd&
//make test

int main() {
  auto app = Gtk::Application::create();
  Gsv::init();

  auto tests_path = boost::filesystem::canonical(JUCI_TESTS_PATH);
  auto source_file = tests_path / "tmp" / "source_file.cpp";

  {
    Source::View source_view(source_file, Glib::RefPtr<Gsv::Language>());
    source_view.get_buffer()->set_text(hello_world);
    g_assert(source_view.save());
  }

  Source::View source_view(source_file, Glib::RefPtr<Gsv::Language>());
  g_assert(source_view.get_buffer()->get_text() == hello_world);
  source_view.cleanup_whitespace_characters();
  g_assert(source_view.get_buffer()->get_text() == hello_world_cleaned);

  g_assert(boost::filesystem::remove(source_file));
  g_assert(!boost::filesystem::exists(source_file));

  for(int c = 0; c < 2; ++c) {
    size_t found = 0;
    auto style_scheme_manager = Source::StyleSchemeManager::get_default();
    for(auto &search_path : style_scheme_manager->get_search_path()) {
      if(search_path == "styles") // found added style
        ++found;
    }
    g_assert(found == 1);
  }

  // replace_text tests
  {
    auto buffer = source_view.get_buffer();
    {
      auto text = "line 1\nline 2\nline3";
      buffer->set_text(text);
      buffer->place_cursor(buffer->begin());
      source_view.replace_text(text);
      assert(buffer->get_text() == text);
      assert(buffer->get_insert()->get_iter() == buffer->begin());

      buffer->place_cursor(buffer->end());
      source_view.replace_text(text);
      assert(buffer->get_text() == text);
      assert(buffer->get_insert()->get_iter() == buffer->end());

      source_view.place_cursor_at_line_offset(1, 0);
      source_view.replace_text(text);
      assert(buffer->get_text() == text);
      assert(buffer->get_insert()->get_iter().get_line() == 1);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);
    }
    {
      auto old_text = "line 1\nline3";
      auto new_text = "line 1\nline 2\nline3";
      buffer->set_text(old_text);
      source_view.place_cursor_at_line_offset(1, 0);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
      assert(buffer->get_insert()->get_iter().get_line() == 2);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);

      source_view.replace_text(old_text);
      assert(buffer->get_text() == old_text);
      assert(buffer->get_insert()->get_iter().get_line() == 1);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);

      source_view.place_cursor_at_line_offset(0, 0);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
      assert(buffer->get_insert()->get_iter().get_line() == 0);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);

      source_view.replace_text(old_text);
      assert(buffer->get_text() == old_text);
      assert(buffer->get_insert()->get_iter().get_line() == 0);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);

      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);

      source_view.place_cursor_at_line_offset(2, 0);
      source_view.replace_text(old_text);
      assert(buffer->get_text() == old_text);
      assert(buffer->get_insert()->get_iter().get_line() == 1);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);
    }
    {
      auto old_text = "line 1\nline 3";
      auto new_text = "";
      buffer->set_text(old_text);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);

      source_view.replace_text(old_text);
      assert(buffer->get_text() == old_text);
      assert(buffer->get_insert()->get_iter().get_line() == 1);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 6);
    }
    {
      auto old_text = "";
      auto new_text = "";
      buffer->set_text(old_text);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
    }
    {
      auto old_text = "line 1\nline 3\n";
      auto new_text = "";
      buffer->set_text(old_text);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);

      source_view.replace_text(old_text);
      assert(buffer->get_text() == old_text);
      assert(buffer->get_insert()->get_iter().get_line() == 2);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);
    }
    {
      auto old_text = "line 1\n\nline 3\nline 4\n\nline 5\n";
      auto new_text = "line 1\n\nline 33\nline 44\n\nline 5\n";
      buffer->set_text(old_text);
      source_view.place_cursor_at_line_offset(2, 0);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
      assert(buffer->get_insert()->get_iter().get_line() == 2);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);

      buffer->set_text(old_text);
      source_view.place_cursor_at_line_offset(3, 0);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
      assert(buffer->get_insert()->get_iter().get_line() == 3);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);
    }
    {
      auto old_text = "line 1\n\nline 3\nline 4\n\nline 5\n";
      auto new_text = "line 1\n\nline 33\nline 44\nline 45\nline 46\n\nline 5\n";
      buffer->set_text(old_text);
      source_view.place_cursor_at_line_offset(2, 0);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
      assert(buffer->get_insert()->get_iter().get_line() == 2);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);

      buffer->set_text(old_text);
      source_view.place_cursor_at_line_offset(3, 0);
      source_view.replace_text(new_text);
      assert(buffer->get_text() == new_text);
      assert(buffer->get_insert()->get_iter().get_line() == 4);
      assert(buffer->get_insert()->get_iter().get_line_offset() == 0);
    }
  }

  // extend_selection() tests
  {
    auto buffer = source_view.get_buffer();
    source_view.is_bracket_language = true;
    std::string source = "test(1, test(10), \"100\");";
    buffer->set_text(source);
    {
      source_view.place_cursor_at_line_offset(0, 0);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test(1, test(10), \"100\")");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }
    {
      source_view.place_cursor_at_line_offset(0, 5);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "1");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "1, test(10), \"100\"");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test(1, test(10), \"100\")");
    }
    {
      source_view.place_cursor_at_line_offset(0, 7);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == " test(10)");
    }
    {
      source_view.place_cursor_at_line_offset(0, 8);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test(10)");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == " test(10)");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "1, test(10), \"100\"");
    }
    {
      source_view.place_cursor_at_line_offset(0, 18);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == " \"100\"");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "1, test(10), \"100\"");
    }
    {
      source_view.place_cursor_at_line_offset(0, 26);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }
    {
      source_view.place_cursor_at_line_offset(0, 27);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }

    source = "int main() {\n  return 1;\n}\n";
    buffer->set_text(source);
    {
      source_view.place_cursor_at_line_offset(0, 0);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "int");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(0, source.size() - 1));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }
    {
      source_view.place_cursor_at_line_offset(0, 4);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "main");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(4, source.size() - 1 - 4));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(0, source.size() - 1));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }
    {
      source_view.place_cursor_at_line_offset(1, 2);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "return");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "return 1;");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(12, 13));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(4, source.size() - 1 - 4));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(0, source.size() - 1));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }

    source = "test<int, int>(11, 22);";
    buffer->set_text(source);
    {
      source_view.place_cursor_at_line_offset(0, 0);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(0, source.size() - 1));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);
    }
    {
      source_view.place_cursor_at_line_offset(0, 5);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "int");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(5, 8));

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(0, source.size() - 1));
    }
    {
      source_view.place_cursor_at_line_offset(0, 15);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "11");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "11, 22");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source.substr(0, source.size() - 1));
    }

    source = "{\n  {\n    test;\n  }\n}\n";
    buffer->set_text(source);
    {
      source_view.place_cursor_at_line_offset(2, 4);
      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "test;");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "\n    test;\n  ");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "{\n    test;\n  }");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "\n  {\n    test;\n  }\n");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == "{\n  {\n    test;\n  }\n}");

      source_view.extend_selection();
      assert(source_view.get_selected_text() == source);

      source_view.shrink_selection();
      assert(source_view.get_selected_text() == "{\n  {\n    test;\n  }\n}");

      source_view.shrink_selection();
      assert(source_view.get_selected_text() == "\n  {\n    test;\n  }\n");

      source_view.shrink_selection();
      assert(source_view.get_selected_text() == "{\n    test;\n  }");
    }
  }
}
