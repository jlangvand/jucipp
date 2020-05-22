# <img alt="juCi++" src="share/juci.png" />

###### juCi++: a lightweight, platform independent C++-IDE with support for C++11, C++14 and C++17 features depending on libclang version.
<!--<img src="https://gitlab.com/cppit/jucipp/raw/master/docs/images/screenshot3.png"/>-->
## About
Current IDEs struggle with C++ support due to the complexity of
the programming language. juCI++, however, is designed especially 
towards libclang with speed, stability, and ease of use in mind. 

## Features
* Platform independent
* Fast, responsive and stable (written extensively using C++11/14 features)
* Syntax highlighting for more than 100 different file types
* C++ warnings and errors on the fly
* C++ Fix-its
* Integrated Clang-Tidy checks can be enabled in preferences
* Debug integration, both local and remote, through lldb
* Supports the following build systems:
    * CMake
    * Meson
* Git support through libgit2
* Fast C++ autocompletion
* Tooltips showing type information and doxygen documentation (C++)
* Rename refactoring across files (C++)
* Highlighting of similar types (C++)
* Automated documentation search (C++)
* Go to declaration, implementation, methods and usages (C++)
* OpenCL and CUDA files are supported and parsed as C++
* Other file types:
    * Language server protocol support is enabled if `[language identifier]-language-server` executable is found. This executable can be a symbolic link to one of your installed language server binaries.
        * For additional instructions, see: [setup of tested language servers](docs/language_servers.md)
    * otherwise, only keyword and buffer completion supported
* Find symbol through Ctags ([Universal Ctags](https://github.com/universal-ctags/ctags) is recommended)
* Spell checking depending on file context
* Run shell commands within juCi++
* Regex search and replace
* Smart paste, keys and indentation
* Extend/shrink selection
* Multiple cursors
* Snippets can be added in ~/.juci/snippets.json using the [TextMate snippet syntax](https://macromates.com/manual/en/snippets). The language ids used in the regexes can be found here: https://gitlab.gnome.org/GNOME/gtksourceview/tree/master/data/language-specs.
* Auto-indentation through [clang-format](http://clang.llvm.org/docs/ClangFormat.html) or [Prettier](https://github.com/prettier/prettier) if installed
* Source minimap
* Split view
* Full UTF-8 support
* Wayland supported with GTK+ 3.20 or newer

See [enhancements](https://gitlab.com/cppit/jucipp/issues?scope=all&state=opened&label_name[]=enhancement) for planned features.

## Screenshots
<table border="0">
<tr>
<td><img src="docs/images/screenshot1c.png" width="380"/></td>
<td><img src="docs/images/screenshot2c.png" width="380"/></td>
</tr><tr>
<td><img src="docs/images/screenshot3c.png" width="380"/></td>
<td><img src="docs/images/screenshot4b.png" width="380"/></td>
</tr>
</table>

## Installation
See [installation guide](docs/install.md).

## Custom styling
See [custom styling](docs/custom_styling.md).

## Dependencies
* boost-filesystem
* boost-serialization
* gtkmm-3.0
* gtksourceviewmm-3.0
* aspell
* libclang
* lldb
* libgit2
* [libclangmm](http://gitlab.com/cppit/libclangmm/) (downloaded directly with git --recursive, no need to install)
* [tiny-process-library](http://gitlab.com/eidheim/tiny-process-library/) (downloaded directly with git --recursive, no need to install)

## Documentation
See [how to build the API doc](docs/api.md).

## Coding style
Due to poor lambda support in clang-format, a custom clang-format is used with the following patch applied:
```diff
diff --git a/lib/Format/ContinuationIndenter.cpp b/lib/Format/ContinuationIndenter.cpp
index bb8efd61a3..e80a487055 100644
--- a/lib/Format/ContinuationIndenter.cpp
+++ b/lib/Format/ContinuationIndenter.cpp
@@ -276,6 +276,8 @@ LineState ContinuationIndenter::getInitialState(unsigned FirstIndent,
 }
 
 bool ContinuationIndenter::canBreak(const LineState &State) {
+  if(Style.ColumnLimit==0)
+    return true;
   const FormatToken &Current = *State.NextToken;
   const FormatToken &Previous = *Current.Previous;
   assert(&Previous == Current.Previous);
@@ -325,6 +327,8 @@ bool ContinuationIndenter::canBreak(const LineState &State) {
 }
 
 bool ContinuationIndenter::mustBreak(const LineState &State) {
+  if(Style.ColumnLimit==0)
+    return false;
   const FormatToken &Current = *State.NextToken;
   const FormatToken &Previous = *Current.Previous;
   if (Current.MustBreakBefore || Current.is(TT_InlineASMColon))
```
