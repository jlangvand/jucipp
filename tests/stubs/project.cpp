#include "project.hpp"

std::shared_ptr<Project::Base> Project::current;

std::shared_ptr<Project::Base> Project::create() { return nullptr; }
