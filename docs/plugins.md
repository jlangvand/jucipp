# Plugins

## Getting started

Plugins are stored in .juci/plugins

### Basic hello world
from Jucipp import Terminal

print("Hello, world! From before the application is loaded")

def init_hook():
    t = Terminal()
    t.print("Hello, world! From after the application is started.\n")
