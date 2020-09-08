from Jucipp import Terminal
from jucipp_test import assert_equal

t = Terminal()

def hello_world():
    t.print("Hello, World!\n")

def clear():
    t.clear()

def process(path):
    p = t.process("ls", path, True)
    assert_equal(p, 0)
    return p

def async_print():
    return t.async_print("Hello, World!")

def callback(exit_code):
    assert_equal(0, exit_code)

def async_process(path):
    p = t.async_process("ls", path, callback, True)
    assert_equal(0, p.get_exit_status())
    return p.get_exit_status()
