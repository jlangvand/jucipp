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

## TypeScript or JavaScript without Flow
* Prerequisites:
    * Node.js
* Recommended:
    * [Prettier](https://github.com/prettier/prettier)

Install language server, and create executable to enable server in juCi++:
```sh
npm install -g typescript-language-server typescript

echo '#!/bin/bash
`npm root -g`/typescript-language-server/lib/cli.js --stdio' > /usr/local/bin/javascript-language-server

chmod 755 /usr/local/bin/javascript-language-server
rm -f /usr/local/bin/typescript-language-server
cp /usr/local/bin/javascript-language-server /usr/local/bin/typescript-language-server
cp /usr/local/bin/javascript-language-server /usr/local/bin/typescriptreact-language-server
```

* Additional setup within a JavaScript project:
    * Add a `.prettierrc` file to enable style format on save

## Python3
* Prerequisites:
    * Python3
    * In juCi++ preferences, set `project.python_command` to `PYTHONUNBUFFERED=1 python3`

Install language server, and create symbolic link to enable server in juCi++:
```sh
pip3 install python-language-server[rope,pycodestyle,yapf]

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
