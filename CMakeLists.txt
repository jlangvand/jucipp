cmake_minimum_required(VERSION 3.1)

project(juci)
set(JUCI_VERSION "1.7.1")

set(CPACK_PACKAGE_NAME "jucipp")
set(CPACK_PACKAGE_CONTACT "Ole Christian Eidheim <eidheim@gmail.com>")
set(CPACK_PACKAGE_VENDOR ${CPACK_PACKAGE_CONTACT})
set(CPACK_PACKAGE_VERSION ${JUCI_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight, platform independent IDE made especially for C/C++.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_DEBIAN_PACKAGE_DEPENDS "cmake, make, g++, libclang-dev, liblldb-dev, clang-format, pkg-config, libboost-system-dev, libboost-filesystem-dev, libboost-serialization-dev libgtksourceviewmm-3.0-dev, aspell-en, libaspell-dev, libgit2-dev, universal-ctags")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://gitlab.com/cppit/jucipp")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
  message(STATUS "using ccache.")
else()
  message(STATUS "ccache was not found.")
endif(CCACHE_FOUND)

include(CPack)

set(CMAKE_CXX_STANDARD 14)

add_compile_options(-pthread -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)
add_definitions(-DJUCI_VERSION="${JUCI_VERSION}")
if(CMAKE_BUILD_TYPE STREQUAL "")
  add_compile_options(-O3)
endif()

# temporarily disable these warnings: Only <gtksourceview/gtksource.h> can be included directly.
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-cpp)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options("-Wno-#warnings")
  add_compile_options(-Wthread-safety -Wno-deprecated -Wstring-conversion -Wliteral-conversion)
endif()

if(APPLE)
  # Added if expressions to avoid linking warnings:
  if(EXISTS /usr/local/lib)
    link_directories(/usr/local/lib)
  endif()
  if(EXISTS /usr/local/opt/gettext/lib)
    link_directories(/usr/local/opt/gettext/lib)
  endif()
  if(EXISTS /usr/local/opt/libsigc++@2/lib)
    link_directories(/usr/local/opt/libsigc++@2/lib)
  endif()
  if(EXISTS /opt/homebrew/lib)
    link_directories(/opt/homebrew/lib)
  endif()

  include_directories(/usr/local/opt/gettext/include)
  set(CMAKE_MACOSX_RPATH 1)
  set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/opt/X11/lib/pkgconfig:/usr/local/opt/libffi/lib/pkgconfig:/usr/local/opt/libsigc++@2/lib/pkgconfig:/usr/local/opt/zlib/lib/pkgconfig:/usr/local/opt/libxml2/lib/pkgconfig")
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES FreeBSD)
  link_directories(/usr/local/lib)
  link_directories(${CMAKE_INSTALL_PREFIX}/lib)
endif()

option(BUILD_TESTING "Build tests")
option(BUILD_FUZZING "Build tests")
option(LIBCLANG_PATH "Use custom path for libclang")
option(LIBLLDB_PATH "Use custom path for liblldb")

find_package(Boost 1.54 COMPONENTS REQUIRED filesystem serialization)
find_package(ASPELL REQUIRED)
include(FindPkgConfig)
pkg_check_modules(GTKMM gtkmm-3.0 REQUIRED)
pkg_check_modules(GTKSVMM gtksourceviewmm-3.0 REQUIRED)
pkg_check_modules(LIBGIT2 libgit2 REQUIRED)

include(FindPackageHandleStandardArgs)

if(LIBCLANG_PATH)
  find_library(LIBCLANG_LIBRARIES clang "${LIBCLANG_PATH}/lib" NO_DEFAULT_PATH)
  set(LIBCLANG_INCLUDE_DIRS "${LIBCLANG_PATH}/include")
  if(LIBCLANG_LIBRARIES)
    find_package_handle_standard_args(libclang DEFAULT_MSG LIBCLANG_LIBRARIES LIBCLANG_INCLUDE_DIRS)
  else()
    message(FATAL_ERROR "libclang not found")
  endif()
else()
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/lib/libclangmm/cmake_modules/")
  find_package(LibClang REQUIRED)
endif()

set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "Disable sub-project tests" FORCE)
add_subdirectory(lib/tiny-process-library)
add_subdirectory(lib/libclangmm)
set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "Set to previous value" FORCE)

if(LIBLLDB_PATH)
  find_library(LIBLLDB_LIBRARIES lldb "${LIBLLDB_PATH}/lib" NO_DEFAULT_PATH)
  set(LIBLLDB_INCLUDE_DIRS "${LIBLLDB_PATH}/include")
else()
  # Find liblldb with the same version as the version of libclang found
  string(REPLACE libclang liblldb LIBLLDB_LIBRARIES "${LIBCLANG_LIBRARIES}")
  set(LIBLLDB_INCLUDE_DIRS ${LIBCLANG_INCLUDE_DIRS})
endif()
if(EXISTS "${LIBLLDB_LIBRARIES}")
  set(LIBLLDB_FOUND TRUE)
elseif(EXISTS "${LIBLLDB_LIBRARIES}.1")
  set(LIBLLDB_LIBRARIES "${LIBLLDB_LIBRARIES}.1")
  set(LIBLLDB_FOUND TRUE)
endif()
if(LIBLLDB_FOUND)
  add_definitions(-DJUCI_ENABLE_DEBUG)
  include_directories(${LIBLLDB_INCLUDE_DIRS})
  find_package_handle_standard_args(liblldb DEFAULT_MSG LIBLLDB_LIBRARIES LIBLLDB_INCLUDE_DIRS)
else()
  set(LIBLLDB_LIBRARIES "")
  message("liblldb not found. Building juCi++ without debugging support")
endif()

if(CMAKE_SYSTEM_NAME MATCHES .*BSD|DragonFly)
  add_definitions(-DJUCI_USE_UCTAGS) # See https://svnweb.freebsd.org/ports?view=revision&revision=452957
  add_definitions(-DJUCI_USE_GREP_EXCLUDE) # --exclude-dir is not an argument in bsd grep
endif()

# For both src and tests targets
include_directories(
  ${Boost_INCLUDE_DIRS}
  ${GTKMM_INCLUDE_DIRS}
  ${GTKSVMM_INCLUDE_DIRS}
  ${LIBCLANG_INCLUDE_DIRS}
  ${ASPELL_INCLUDE_DIR}
  ${LIBGIT2_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/lib/json/include
)

add_subdirectory("src")

if(BUILD_TESTING OR BUILD_FUZZING)
  if(BUILD_TESTING)
    enable_testing()
  endif()
  add_subdirectory(tests)
endif()
