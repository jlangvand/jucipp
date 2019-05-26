from Jucipp import CompileCommands

from os import path

from jucipp_test import assert_equal

def run(project_path, slash):
    build_path = project_path + slash + "build"
    cc = CompileCommands(build_path)
    commands = cc.commands
    assert len(commands) == 1, "Wrong length of compile commands"
    command = commands.pop()
    assert_equal(build_path, command.directory)
    assert_equal(project_path + slash + "main.cpp", command.file)

    params = command.parameters
    param = path.basename(params.pop())
    assert_equal("main.cpp", param)

    param = params.pop()
    assert_equal("-c", param)

    param = params.pop()
    param = params.pop()
    assert_equal("-o", param)

    values = command.parameter_values("-c")
    value = path.basename(values.pop())
    assert_equal("main.cpp", value)

    assert_equal(True, CompileCommands.is_source(project_path + slash + "main.cpp"))
    assert_equal(False, CompileCommands.is_header(project_path + slash + "main.cpp"))

    arguments = CompileCommands.get_arguments(build_path, project_path + slash + "main.cpp")
    argument = arguments.pop()

    assert_equal(build_path, argument)
