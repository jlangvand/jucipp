#include "selection_dialog.hpp"

SelectionDialogBase::ListViewText::ListViewText(bool use_markup) {}

SelectionDialogBase::SelectionDialogBase(Gtk::TextView *text_view, const boost::optional<Gtk::TextIter> &start_iter, bool show_search_entry, bool use_markup)
    : text_view(text_view), list_view_text(use_markup) {}

void SelectionDialogBase::show() {}

void SelectionDialogBase::hide() {}

void SelectionDialogBase::add_row(const std::string &row) {}

std::unique_ptr<SelectionDialog> SelectionDialog::instance;

SelectionDialog::SelectionDialog(Gtk::TextView *text_view, const boost::optional<Gtk::TextIter> &start_iter, bool show_search_entry, bool use_markup)
    : SelectionDialogBase(text_view, start_iter, show_search_entry, use_markup) {}

bool SelectionDialog::on_key_press(GdkEventKey *event) { return true; }

std::unique_ptr<CompletionDialog> CompletionDialog::instance;

CompletionDialog::CompletionDialog(Gtk::TextView *text_view, const Gtk::TextIter &start_iter)
    : SelectionDialogBase(text_view, start_iter, false, false) {}

bool CompletionDialog::on_key_press(GdkEventKey *event) { return true; }

bool CompletionDialog::on_key_release(GdkEventKey *event) { return true; }
