from Jucipp import Ctags
from jucipp_test import assert_equal

def get_location(a, b):
    line = 'main	main.cpp	/^int main() { return 0; }$/;"	line:1'
    location = Ctags.get_location(line, False)
    if location:
        assert_equal('main.cpp', location.file_path)
        assert_equal(0, location.line)
        assert_equal(4, location.index)
        assert_equal('main', location.symbol)
        assert_equal('', location.scope)
        assert_equal('int main() { return 0; }', location.source)
    else:
        raise ValueError('File path was empty')

def get_locations(project_path, slash):
    path = project_path + slash + 'main.cpp'
    locations = Ctags.get_locations(project_path + slash + 'main.cpp', 'main', 'int ()')
    assert_equal(len(locations), 1)
    location = locations[0];
    assert_equal(path, location.file_path)
    assert_equal(0, location.line)
    assert_equal(4, location.index)
    assert_equal('main', location.symbol)
    assert_equal('', location.scope)
    assert_equal('int main() { return 0; }', location.source)
