#include "test_suite.h"

int main() {
  suite test_suite("CMake_tests");
  py::module::import("cmake_test");
}