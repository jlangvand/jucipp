# Setup of tested language servers

## JavaScript with Flow static type checker
* Prerequisites:
    * Node.js
* Recommended:
    * [Prettier](https://github.com/prettier/prettier)

Install language server, and create executable to enable server in juCi++:
```sh
npm install -g flow-bin

echo '#!/bin/bash
flow lsp' > /usr/local/bin/javascript-language-server

chmod 755 /usr/local/bin/javascript-language-server
```

* Additional setup within a JavaScript project:
    * Add a `.prettierrc` file to enable style format on save

## Python3
* Prerequisites:
    * In juCi++ preferences, set `project.python_command` to `PYTHONUNBUFFERED=1 python3`
    * Python3
        * MacOS: Jedi does not yet support Python 3.7, but you can install latest 3.6 version:
            
            `brew install https://raw.githubusercontent.com/Homebrew/homebrew-core/f2a764ef944b1080be64bd88dca9a1d80130c558/Formula/python.rb`

Install language server, and create symbolic link to enable server in juCi++:
```sh
pip3 install python-language-server[rope,pycodestyle,yapf] pyls-mypy

ln -s `which pyls` /usr/local/bin/python-language-server
```

* Additional setup within a Python project:
    * Add a setup file, for instance: `printf '[pycodestyle]\nmax-line-length = 120\n\n[yapf]\nCOLUMN_LIMIT = 120\n' > setup.cfg`
    * Add an empty `.python-format` file to enable style format on save

## Rust
* Prerequisites:
    * Rust
      
Install language server, and create symbolic link to enable server in juCi++:
```sh
rustup update
rustup component add rls rust-analysis rust-src

ln -s `rustc --print sysroot`/bin/rls /usr/local/bin/rust-language-server
```

* Additional setup within a Rust project:
    * Add an empty `.rust-format` file to enable style format on save
