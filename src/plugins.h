#pragma once
#include "config.hpp"
#include "python_bind.h"
#include <pybind11/embed.h>

class __attribute__((visibility("default")))
Plugins {
public:
  Plugins(Config &config);
  ~Plugins();
  void load();

private:
  py::detail::embedded_module jucipp_module;
};
