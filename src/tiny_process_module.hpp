#include "python_bind.h"

class TinyProcessModule {
public:
  static void init_module(py::module &api);
};