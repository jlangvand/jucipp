from Jucipp import CompileCommands

def run(project_path):
    build_path = project_path + "/build"
    cc = CompileCommands(build_path)
    commands = cc.commands
    assert len(commands) == 1, "Wrong length of compile commands"
    command = commands.pop()
    assert command.directory == build_path
    assert command.file == project_path + "/main.cpp"

    params = command.parameters
    param = params.pop()
    assert param == project_path + "/main.cpp"

    param = params.pop()
    assert param == "-c"

    param = params.pop()
    param = params.pop()
    assert param == "-o"

    values = command.parameter_values("-c")
    value = values.pop()
    assert value == project_path + "/main.cpp"

    assert CompileCommands.is_source(project_path + "/main.cpp") == True
    assert CompileCommands.is_header(project_path + "/main.cpp") == False

    arguments = CompileCommands.get_arguments(build_path, project_path + "/main.cpp")
    argument = arguments.pop()
    assert argument == build_path
