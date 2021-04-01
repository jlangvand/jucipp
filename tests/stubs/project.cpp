#include "project.hpp"

std::shared_ptr<Project::Base> Project::current;

std::shared_ptr<Project::Base> Project::create() { return nullptr; }

std::pair<std::string, std::string> Project::Base::get_run_arguments() {
  return std::make_pair<std::string, std::string>("", "");
}

void Project::Base::compile() {}

void Project::Base::compile_and_run() {}

void Project::Base::recreate_build() {}

std::pair<std::string, std::string> Project::Base::debug_get_run_arguments() {
  return std::make_pair<std::string, std::string>("", "");
}

void Project::Base::debug_start() {}
