from Jucipp import LLDB
from time import sleep
from jucipp_test import assert_equal


exited = False

def on_exit(exit_code):
    assert_equal(0, exit_code)
    global exited
    exited = True

def start_on_exit(exec_path):
    print(exec_path)
    l = LLDB()
    l.on_exit = [on_exit]
    l.start(exec_path, "", [])

    while not exited:
        sleep(0.1)
    LLDB.destroy()
