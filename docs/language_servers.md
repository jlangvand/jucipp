# Setup of tested language servers

## JavaScript/TypeScript

### JavaScript with Flow static type checker
* Prerequisites:
    * Node.js
* Recommended:
    * [Prettier](https://github.com/prettier/prettier)

Install language server, and create executable to enable server in juCi++:
```sh
npm install -g flow-bin

# Usually as root:
echo '#!/bin/sh
flow lsp' > /usr/local/bin/javascript-language-server
chmod 755 /usr/local/bin/javascript-language-server
```

* Additional setup within a JavaScript project:
    * Add a `.prettierrc` file to enable style format on save

### TypeScript or JavaScript without Flow
* Prerequisites:
    * Node.js
* Recommended:
    * [Prettier](https://github.com/prettier/prettier)

Install language server, and create executable to enable server in juCi++:
```sh
npm install -g typescript-language-server typescript

# Usually as root:
echo '#!/bin/sh
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

Install language server, and create symbolic link to enable server in juCi++:
```sh
pip3 install python-language-server[rope,pycodestyle,yapf]

# Usually as root:
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
rustup component add rust-src

git clone https://github.com/rust-analyzer/rust-analyzer
cd rust-analyzer
cargo xtask install --server

# Usually as root:
ln -s ~/.cargo/bin/rust-analyzer /usr/local/bin/rust-language-server
```

* Additional setup within a Rust project:
    * Add an empty `.rust-format` file to enable style format on save

## GLSL
Install language server, and create a script to enable server in juCi++:
```sh
git clone https://github.com/svenstaro/glsl-language-server --recursive
cd glsl-language-server
mkdir build
cd build
cmake ..
make
# Usually as root:
make install
echo '#!/bin/sh
/usr/local/bin/glslls --stdin' > /usr/local/bin/glsl-language-server
chmod 755 /usr/local/bin/glsl-language-server
```
