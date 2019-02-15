# Files used both in ../src and ../tests
set(JUCI_SHARED_FILES
  autocomplete.cpp
  cmake.cpp
  compile_commands.cpp
  ctags.cpp
  dispatcher.cpp
  documentation.cpp
  filesystem.cpp
  git.cpp
  grep.cpp
  menu.cpp
  meson.cpp
  project_build.cpp
  snippets.cpp
  source.cpp
  source_base.cpp
  source_clang.cpp
  source_diff.cpp
  source_generic.cpp
  source_language_protocol.cpp
  source_spellcheck.cpp
  terminal.cpp
  tooltips.cpp
  usages_clang.cpp
  utility.cpp
)
if(LIBLLDB_FOUND)
  list(APPEND JUCI_SHARED_FILES debug_lldb.cpp)
endif()
add_library(juci_shared STATIC ${JUCI_SHARED_FILES})
target_link_libraries(juci_shared
  ${ASPELL_LIBRARIES}
  ${GTKMM_LIBRARIES}
  ${GTKSVMM_LIBRARIES}
  ${LIBGIT2_LIBRARIES}
  ${LIBLLDB_LIBRARIES}
  Boost::filesystem
  Boost::serialization
  clangmm
  tiny-process-library
)

set(JUCI_SOURCES
  config.cc
  dialogs.cc
  dialogs_unix.cc
  directories.cc
  entrybox.cc
  info.cc
  juci.cc
  notebook.cc
  project.cc
  selection_dialog.cc
  tooltips.cc
  window.cc
)

if(APPLE)
  list(APPEND JUCI_SOURCES window_macos.m)
endif()


set(JUCI_TARGET_LIBRARIES
  juci_shared
)

if(${PYTHONLIBS_FOUND})
  list(APPEND JUCI_TARGET_LIBRARIES pybind11 ${PYTHON_LIBRARIES} ${PYGOBJECT_LIBRARIES})
endif()

add_executable(juci ${JUCI_SOURCES})
target_link_libraries(juci ${JUCI_TARGET_LIBRARIES})

if(APPLE)
  target_link_libraries(juci "-framework Foundation -framework AppKit")
endif()

install(TARGETS juci RUNTIME DESTINATION bin)
if(${CMAKE_SYSTEM_NAME} MATCHES Linux|.*BSD|DragonFly)
  install(FILES "${CMAKE_SOURCE_DIR}/share/juci.desktop"
          DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
  install(FILES "${CMAKE_SOURCE_DIR}/share/juci.svg"
          DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps")
elseif(APPLE)
  install(CODE "execute_process(COMMAND /usr/bin/python ${CMAKE_SOURCE_DIR}/share/set_icon_macos.py ${CMAKE_SOURCE_DIR}/share/juci.png ${CMAKE_INSTALL_PREFIX}/bin/juci)")
endif()

# add a target to generate API documentation with Doxygen
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/share/cmake_modules/")
find_package(Plantuml)
find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen to ${CMAKE_CURRENT_BINARY_DIR}" VERBATIM
  )
endif(DOXYGEN_FOUND)
