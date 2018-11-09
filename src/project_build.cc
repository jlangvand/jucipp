#include "project_build.h"
#include "config.h"
#include "filesystem.h"
#include <boost/algorithm/string.hpp>

std::unique_ptr<Project::Build> Project::Build::create(const boost::filesystem::path &path) {
  auto search_path = boost::filesystem::is_directory(path) ? path : path.parent_path();

  while(true) {
    if(boost::filesystem::exists(search_path / "CMakeLists.txt")) {
      std::unique_ptr<Project::Build> build(new CMakeBuild(path));
      if(!build->project_path.empty())
        return build;
      else
        return std::make_unique<Project::Build>();
    }

    if(boost::filesystem::exists(search_path / "meson.build")) {
      std::unique_ptr<Project::Build> build(new MesonBuild(path));
      if(!build->project_path.empty())
        return build;
    }

    if(boost::filesystem::exists(search_path / Config::get().project.default_build_path / "compile_commands.json")) {
      std::unique_ptr<Project::Build> build(new CompileCommandsBuild(search_path));
      return build;
    }

    if(boost::filesystem::exists(search_path / "Cargo.toml")) {
      std::unique_ptr<Project::Build> build(new CargoBuild());
      build->project_path = search_path;
      return build;
    }

    if(boost::filesystem::exists(search_path / "package.json")) {
      std::unique_ptr<Project::Build> build(new NpmBuild());
      build->project_path = search_path;
      return build;
    }

    if(boost::filesystem::exists(search_path / "__main__.py")) {
      std::unique_ptr<Project::Build> build(new PythonMain());
      build->project_path = search_path;
      return build;
    }

    if(search_path == search_path.root_directory())
      break;
    search_path = search_path.parent_path();
  }

  return std::make_unique<Project::Build>();
}

boost::filesystem::path Project::Build::get_default_path() {
  if(project_path.empty())
    return boost::filesystem::path();

  boost::filesystem::path default_build_path = Config::get().project.default_build_path;
  auto default_build_path_string = default_build_path.string();

  boost::replace_all(default_build_path_string, "<project_directory_name>", project_path.filename().string());
  default_build_path = default_build_path_string;

  if(default_build_path.is_relative())
    default_build_path = project_path / default_build_path;

  return filesystem::get_normal_path(default_build_path);
}

boost::filesystem::path Project::Build::get_debug_path() {
  if(project_path.empty())
    return boost::filesystem::path();

  boost::filesystem::path debug_build_path = Config::get().project.debug_build_path;
  auto debug_build_path_string = debug_build_path.string();

  boost::replace_all(debug_build_path_string, "<default_build_path>", Config::get().project.default_build_path);

  boost::replace_all(debug_build_path_string, "<project_directory_name>", project_path.filename().string());
  debug_build_path = debug_build_path_string;

  if(debug_build_path.is_relative())
    debug_build_path = project_path / debug_build_path;

  return filesystem::get_normal_path(debug_build_path);
}

Project::CMakeBuild::CMakeBuild(const boost::filesystem::path &path) : Project::Build(), cmake(path) {
  project_path = cmake.project_path;
}

bool Project::CMakeBuild::update_default(bool force) {
  return cmake.update_default_build(get_default_path(), force);
}

bool Project::CMakeBuild::update_debug(bool force) {
  return cmake.update_debug_build(get_debug_path(), force);
}

std::string Project::CMakeBuild::get_compile_command() {
  return Config::get().project.cmake.compile_command;
}

boost::filesystem::path Project::CMakeBuild::get_executable(const boost::filesystem::path &path) {
  return cmake.get_executable(get_default_path(), path).string();
}

Project::MesonBuild::MesonBuild(const boost::filesystem::path &path) : Project::Build(), meson(path) {
  project_path = meson.project_path;
}

bool Project::MesonBuild::update_default(bool force) {
  return meson.update_default_build(get_default_path(), force);
}

bool Project::MesonBuild::update_debug(bool force) {
  return meson.update_debug_build(get_debug_path(), force);
}

std::string Project::MesonBuild::get_compile_command() {
  return Config::get().project.meson.compile_command;
}

boost::filesystem::path Project::MesonBuild::get_executable(const boost::filesystem::path &path) {
  return meson.get_executable(get_default_path(), path);
}

Project::CompileCommandsBuild::CompileCommandsBuild(const boost::filesystem::path &path) {
  project_path = path;
}
