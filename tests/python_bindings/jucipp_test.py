""" Shared test methods """

def assert_equal(expected, actual):
    """ Assert two variables for equality with an useful error message """
    assert actual == expected, "Expected: " + str(expected) + ", got " + str(actual)
