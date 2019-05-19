from Jucipp import CMake

def run(project_path):
    cmake = CMake(project_path)
    assert project_path == cmake.project_path, "Construction of CMake failed"
    default_build_path = project_path + "/build"
    assert cmake.update_default_build(default_build_path) == True, "Update of default build failed"
    executable = cmake.get_executable(default_build_path, project_path)
    assert executable == default_build_path + "/cmake_project", "Invalid executable"
    default_debug_path = project_path + "/debug"
    assert cmake.update_debug_build(default_debug_path), "Update of debug build failed"
    executable = cmake.get_executable(default_debug_path, project_path)
