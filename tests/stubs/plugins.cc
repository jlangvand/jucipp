#include "plugins.h"
#include "python_module.h"

Plugins::Plugins() : jucipp_module("Jucipp", Module::init_jucipp_module) {}

void Plugins::load() {}

Plugins::~Plugins() {}
