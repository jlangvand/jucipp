#include "python_type_casters.h"
#include <test_suite.h>

int main() {
  {
    suite test_suite("PythonModule_tests");
    {
      py::module::import("python_module_test");
      test_suite.has_assertion = true;
    }
  }
  {
    suite test_suite("PythonModule_tests");
    {
      try {
        py::module::import("exception_test");
      }
      catch(const py::error_already_set &error) {
        test_suite.has_assertion = true;
      }
    }
  }

  return 0;
}
