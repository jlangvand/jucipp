#include "tooltips.hpp"
#include <glib.h>
#include <gtkmm.h>

int main() {
  auto app = Gtk::Application::create();

  auto get_markdown_tooltip = [](const std::string &input) {
    auto tooltip = std::make_unique<Tooltip>([&](Tooltip &tooltip) {
      tooltip.insert_markdown(input);
    });
    tooltip->show();
    return tooltip;
  };

  // Testing insert_markdown():
  {
    auto tooltip = get_markdown_tooltip("");
    g_assert(tooltip->buffer->get_text() == "");
  }
  {
    auto tooltip = get_markdown_tooltip("test");
    g_assert(tooltip->buffer->get_text() == "test");
  }
  {
    auto tooltip = get_markdown_tooltip("test\ntest");
    g_assert(tooltip->buffer->get_text() == "test\ntest");
  }
  {
    auto tooltip = get_markdown_tooltip("test\n\ntest");
    g_assert(tooltip->buffer->get_text() == "test\n\ntest");
  }
  {
    auto tooltip = get_markdown_tooltip("test\n\ntest\n\n");
    g_assert(tooltip->buffer->get_text() == "test\n\ntest");
  }
  {
    auto tooltip = get_markdown_tooltip("\\# test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "# test");
  }
  {
    auto tooltip = get_markdown_tooltip("# test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->h1_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h1_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("# test\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test\n\ntest");
    g_assert(buffer->begin().starts_tag(tooltip->h1_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h1_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("# test\n\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test\n\ntest");
    g_assert(buffer->begin().starts_tag(tooltip->h1_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h1_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("test\n# test\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test\n\ntest\n\ntest");
    g_assert(buffer->get_iter_at_offset(6).starts_tag(tooltip->h1_tag));
    g_assert(buffer->get_iter_at_offset(10).ends_tag(tooltip->h1_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("## test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->h2_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h2_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("test\n====");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->h1_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h1_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("test\n----");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->h2_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h2_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("### test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->h3_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->h3_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("_test_");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("*test*");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
    g_assert(!buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(!buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("*test* test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test test");
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
    g_assert(!buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(!buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("**test**");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
    g_assert(!buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(!buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("__test__");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
    g_assert(!buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(!buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("*_test_*");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
    g_assert(!buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(!buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("***test***");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("**_test_**");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->bold_tag));
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("~~test~~");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->strikethrough_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->strikethrough_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("~~test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "~~test");
  }
  {
    auto tooltip = get_markdown_tooltip("~~test~~ test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test test");
    g_assert(buffer->begin().starts_tag(tooltip->strikethrough_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->strikethrough_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("~~*test*~~");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->strikethrough_tag));
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->strikethrough_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("test_test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test_test");
  }
  {
    auto tooltip = get_markdown_tooltip("_test_test_");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test_test");
    g_assert(buffer->begin().starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(9).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("2 * 2 * 2");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "2 * 2 * 2");
  }
  {
    auto tooltip = get_markdown_tooltip("* *2*");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "* 2");
  }
  {
    auto tooltip = get_markdown_tooltip("* 2*");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "* 2*");
  }
  {
    auto tooltip = get_markdown_tooltip("*");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "*");
  }
  {
    auto tooltip = get_markdown_tooltip("\\*test\\*");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "*test*");
  }
  {
    auto tooltip = get_markdown_tooltip("*\\*test\\**");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "*test*");
    g_assert(buffer->get_iter_at_offset(0).starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(6).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("**test _test_**");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test test");
    g_assert(buffer->get_iter_at_offset(0).starts_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(9).ends_tag(tooltip->bold_tag));
    g_assert(buffer->get_iter_at_offset(5).starts_tag(tooltip->italic_tag));
    g_assert(buffer->get_iter_at_offset(9).ends_tag(tooltip->italic_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("_test _test_");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "_test test");
  }
  {
    auto tooltip = get_markdown_tooltip("`test`");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test");
    g_assert(buffer->begin().starts_tag(tooltip->code_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->code_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("test `test` test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test test test");
    g_assert(buffer->get_iter_at_offset(5).starts_tag(tooltip->code_tag));
    g_assert(buffer->get_iter_at_offset(9).ends_tag(tooltip->code_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("``te`st``");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "te`st");
    g_assert(buffer->begin().starts_tag(tooltip->code_tag));
    g_assert(buffer->get_iter_at_offset(5).ends_tag(tooltip->code_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("# Test\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "Test\n\ntest");
  }
  {
    auto tooltip = get_markdown_tooltip("test\n\n# Test\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test\n\nTest\n\ntest");
  }
  {
    auto tooltip = get_markdown_tooltip("```\ntest\n```\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test\n\ntest");
    g_assert(buffer->begin().starts_tag(tooltip->code_block_tag));
    g_assert(buffer->get_iter_at_offset(5).ends_tag(tooltip->code_block_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("test\n```c++\ntest\n```\ntest");
    auto buffer = tooltip->buffer;
    g_assert(buffer->get_text() == "test\n\ntest\n\ntest");
    g_assert(buffer->get_iter_at_offset(6).starts_tag(tooltip->code_block_tag));
    g_assert(buffer->get_iter_at_offset(11).ends_tag(tooltip->code_block_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("http://test.com");
    g_assert(tooltip->buffer->get_text() == "http://test.com");
    auto buffer = tooltip->buffer;
    g_assert(buffer->begin().starts_tag(tooltip->link_tag));
    g_assert(buffer->get_iter_at_offset(15).ends_tag(tooltip->link_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("http://test.com.");
    g_assert(tooltip->buffer->get_text() == "http://test.com.");
    auto buffer = tooltip->buffer;
    g_assert(buffer->begin().starts_tag(tooltip->link_tag));
    g_assert(buffer->get_iter_at_offset(15).ends_tag(tooltip->link_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("[test](http://test.com)");
    g_assert(tooltip->buffer->get_text() == "test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->begin().starts_tag(tooltip->link_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->link_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("[`test`](http://test.com)");
    g_assert(tooltip->buffer->get_text() == "test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->begin().starts_tag(tooltip->link_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->link_tag));
    g_assert(buffer->begin().starts_tag(tooltip->code_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->code_tag));
  }
  {
    auto tooltip = get_markdown_tooltip("[1]: http://test.com");
    g_assert(tooltip->buffer->get_text() == "");
  }
  {
    auto tooltip = get_markdown_tooltip("[`test`]\n\n[`test`]: http://test.com");
    g_assert(tooltip->buffer->get_text() == "test");
    auto buffer = tooltip->buffer;
    g_assert(buffer->begin().starts_tag(tooltip->link_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->link_tag));
    g_assert(buffer->begin().starts_tag(tooltip->code_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->code_tag));

    g_assert(tooltip->reference_links.size() == 1);
    g_assert(tooltip->reference_links.begin()->second == "test");
    g_assert(tooltip->references.size() == 1);
    g_assert(tooltip->references.begin()->second == "http://test.com");
  }
  {
    auto tooltip = get_markdown_tooltip("[`text`][test]\n\n[test]: http://test.com");
    g_assert(tooltip->buffer->get_text() == "text");
    auto buffer = tooltip->buffer;
    g_assert(buffer->begin().starts_tag(tooltip->link_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->link_tag));
    g_assert(buffer->begin().starts_tag(tooltip->code_tag));
    g_assert(buffer->get_iter_at_offset(4).ends_tag(tooltip->code_tag));

    g_assert(tooltip->reference_links.size() == 1);
    g_assert(tooltip->reference_links.begin()->second == "test");
    g_assert(tooltip->references.size() == 1);
    g_assert(tooltip->references.begin()->second == "http://test.com");
  }

  // Testing wrap_lines():
  {
    auto tooltip = get_markdown_tooltip("");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "");
  }
  {
    auto tooltip = get_markdown_tooltip("test\ntest");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "test\ntest");
  }
  {
    auto tooltip = get_markdown_tooltip("test test test test test test test test test test test test test test test test test test test test test");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "test test test test test test test test test test test test test test test test\ntest test test test test");
  }
  {
    auto tooltip = get_markdown_tooltip("test test test test test test test test test test test test test test test testt test test test test test");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "test test test test test test test test test test test test test test test testt\ntest test test test test");
  }
  {
    auto tooltip = get_markdown_tooltip("test test test test test test test test test test test test test test test testtt test test test test test");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "test test test test test test test test test test test test test test test\ntesttt test test test test test");
  }
  {
    auto tooltip = get_markdown_tooltip("testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest\ntest test");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest\ntest test");
  }
  {
    auto tooltip = get_markdown_tooltip("testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest test test");
    tooltip->wrap_lines();
    g_assert(tooltip->buffer->get_text() == "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest\ntest test");
  }
}
