#include "window.h"
#include "logging.h"

Window::Window() :
  window_box_(Gtk::ORIENTATION_VERTICAL),
  main_config_(),
  keybindings_(main_config_.keybindings_cfg),
  terminal(main_config_.terminal_cfg),
  notebook(keybindings(), terminal,
            main_config_.source_cfg,
            main_config_.dir_cfg),
  menu_(keybindings()),
  api_(menu_, notebook) {
  INFO("Create Window");
  set_title("juCi++");
  set_default_size(600, 400);
  set_events(Gdk::POINTER_MOTION_MASK);
  add(window_box_);
  keybindings_.action_group_menu()->add(Gtk::Action::create("FileQuit",
                                                            "Quit juCi++"),
                                        Gtk::AccelKey(keybindings_.config_
                                                      .key_map()["quit"]),
                                        [this]() {
                                          OnWindowHide();
                                        });
  keybindings_.action_group_menu()->add(Gtk::Action::create("FileOpenFile",
                                                            "Open file"),
                                        Gtk::AccelKey(keybindings_.config_
                                                      .key_map()["open_file"]),
                                        [this]() {
                                          OnOpenFile();
                                        });
  keybindings_.action_group_menu()->add(Gtk::Action::create("FileOpenFolder",
                                                            "Open folder"),
                                        Gtk::AccelKey(keybindings_.config_
                                                      .key_map()["open_folder"]),
                                        [this]() {
                                          OnFileOpenFolder();
                                        });
  keybindings_.
    action_group_menu()->
    add(Gtk::Action::create("FileSaveAs",
			    "Save as"),
	Gtk::AccelKey(keybindings_.config_
		      .key_map()["save_as"]),
	[this]() {
	  SaveFileAs();
	});

  keybindings_.
    action_group_menu()->
    add(Gtk::Action::create("FileSave",
			    "Save"),
	Gtk::AccelKey(keybindings_.config_
		      .key_map()["save"]),
	[this]() {
	  SaveFile();
	});
  
  keybindings_.
    action_group_menu()->
    add(Gtk::Action::create("ProjectCompileAndRun",
			    "Compile And Run"),
	Gtk::AccelKey(keybindings_.config_
		      .key_map()["compile_and_run"]),
	[this]() {
	  SaveFile();
	  if (running.try_lock()) {
	    std::thread execute([=]() {
		std::string path = notebook.CurrentTextView().file_path;
		size_t pos = path.find_last_of("/\\");
		if(pos != std::string::npos) {
		  path.erase(path.begin()+pos,path.end());
		  terminal.SetFolderCommand(path);
		}
		terminal.Compile();
		std::string executable = notebook.directories.
		  GetCmakeVarValue(path,"add_executable");
		terminal.Run(executable);
                running.unlock();
	      });
	    execute.detach();
	  }
	});
   
  keybindings_.
    action_group_menu()->
    add(Gtk::Action::create("ProjectCompile",
                            "Compile"),
        Gtk::AccelKey(keybindings_.config_
                      .key_map()["compile"]),
        [this]() {
          SaveFile();
          if (running.try_lock()) {
            std::thread execute([=]() {		  
                std::string path =  notebook.CurrentTextView().file_path;
                size_t pos = path.find_last_of("/\\");
                if(pos != std::string::npos){
                  path.erase(path.begin()+pos,path.end());
                  terminal.SetFolderCommand(path);
                }
                terminal.Compile();
                running.unlock();
              });
            execute.detach();
          }
        });

  add_accel_group(keybindings_.ui_manager_menu()->get_accel_group());
  add_accel_group(keybindings_.ui_manager_hidden()->get_accel_group());
  keybindings_.BuildMenu();

  window_box_.pack_start(menu_.view(), Gtk::PACK_SHRINK);

  window_box_.pack_start(notebook.entry, Gtk::PACK_SHRINK);
  paned_.set_position(300);
  paned_.pack1(notebook.view, true, false);
  paned_.pack2(terminal.view, true, true);
  window_box_.pack_end(paned_);
  show_all_children();
  INFO("Window created");
} // Window constructor

void Window::OnWindowHide() {
  for(size_t c=0;c<notebook.text_vec_.size();c++)
    notebook.OnCloseCurrentPage(); //TODO: This only works on one page at the momemt. Change to notebook.close_page(page_nr);
  hide();
}
void Window::OnFileOpenFolder() {
  Gtk::FileChooserDialog dialog("Please choose a folder",
                                Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
  dialog.set_transient_for(*this);
  //Add response buttons the the dialog:
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("Select", Gtk::RESPONSE_OK);

  int result = dialog.run();

  //Handle the response:
  switch(result)
    {
    case(Gtk::RESPONSE_OK):
      {
        std::string project_path=dialog.get_filename();
        notebook.project_path=project_path;
        notebook.directories.open_folder(project_path);
        break;
      }
    case(Gtk::RESPONSE_CANCEL):
      {
        break;
      }
    default:
      {
        break;
      }
    }
}


void Window::OnOpenFile() {
  Gtk::FileChooserDialog dialog("Please choose a file",
                                Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.set_transient_for(*this);
  dialog.set_position(Gtk::WindowPosition::WIN_POS_CENTER_ALWAYS);

  //Add response buttons the the dialog:
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Open", Gtk::RESPONSE_OK);

  //Add filters, so that only certain file types can be selected:
  Glib::RefPtr<Gtk::FileFilter> filter_text = Gtk::FileFilter::create();
  filter_text->set_name("Text files");
  filter_text->add_mime_type("text/plain");
  dialog.add_filter(filter_text);

  Glib::RefPtr<Gtk::FileFilter> filter_cpp = Gtk::FileFilter::create();
  filter_cpp->set_name("C/C++ files");
  filter_cpp->add_mime_type("text/x-c");
  filter_cpp->add_mime_type("text/x-c++");
  filter_cpp->add_mime_type("text/x-c-header");
  dialog.add_filter(filter_cpp);

  Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
  filter_any->set_name("Any files");
  filter_any->add_pattern("*");
  dialog.add_filter(filter_any);

  int result = dialog.run();

  switch (result) {
  case(Gtk::RESPONSE_OK): {
    std::string path = dialog.get_filename();
    notebook.OnOpenFile(path);
    break;
  }
  case(Gtk::RESPONSE_CANCEL): {
    break;
  }
  default: {
    break;
  }
  }
}

bool Window::SaveFile() {
  if(notebook.OnSaveFile()) {
    terminal.print("File saved to: " +
			   notebook.CurrentTextView().file_path+"\n");
    return true;
  }
  terminal.print("File not saved");
  return false;
}
bool Window::SaveFileAs() {
  if(notebook.OnSaveFile(notebook.OnSaveFileAs())){
    terminal.print("File saved to: " +
			   notebook.CurrentTextView().file_path+"\n");
    return true;
  }
  terminal.print("File not saved");
  return false;
}
