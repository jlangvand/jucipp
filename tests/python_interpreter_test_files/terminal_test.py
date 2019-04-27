from Jucipp import Terminal

t = Terminal()

def hello_world():
    t.print("Hello, World!\n")

def clear():
    t.clear()

def process(path):
    return t.process("ls", path, True)

def async_print():
    return t.async_print("Hello, World!")
