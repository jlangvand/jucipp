from Jucipp import CMake

from jucipp_test import assert_equal

def run(project_path):
    cmake = CMake(project_path)
    assert_equal(project_path, cmake.project_path)
    default_build_path = project_path + "/build"
    assert_equal(True, cmake.update_default_build(default_build_path))
    executable = cmake.get_executable(default_build_path, project_path)
    assert_equal(default_build_path + "/cmake_project", executable)
    default_debug_path = project_path + "/debug"
    assert_equal(True, cmake.update_debug_build(default_debug_path))
    executable = cmake.get_executable(default_debug_path, project_path)
