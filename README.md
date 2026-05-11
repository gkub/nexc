# nexc

`nexc` is the reference compiler for **nex**. It is an educational C++ compiler
project with a hand-written frontend, typed IR, MLIR/LLVM lowering, and native
executable output for small programs.

## Status

Implemented today:

- lexer, parser, AST dumps, semantic checking
- typed IR, MLIR, and LLVM IR dumps
- native executable generation through `build/nexc <file.nexs> -o <output>`
- runtime-backed `print` / `println`
- selected input helpers such as `readln`, `parse_i32`, `parse_u64`,
  `parse_bool`, and `input_ok`

See [`docs/reference/language`](./docs/reference/language/README.md) if you want to learn how to write nex code, and
[`NEXC_HOLY_BOOK.md`](./NEXC_HOLY_BOOK.md) for an educational tour of how the nex compiler works.

## Quick Start

```sh
./nexc.sh check
```

That configures CMake, builds `nexc`, and runs CTest.

Build and run a program:

```sh
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

Useful inspection commands:

```sh
./nexc.sh tokens examples/minimal.nexs
./nexc.sh ast examples/add.nexs
./nexc.sh ir examples/add.nexs
./nexc.sh mlir examples/return_42.nexs
./nexc.sh llvm examples/return_42.nexs
./nexc.sh check-file examples/hello.nexs
```

## Setup

### macOS

Install Xcode Command Line Tools and Homebrew dependencies:

```sh
xcode-select --install
brew install cmake ninja llvm graphviz
```

Homebrew LLVM is keg-only on macOS. Prefer project-local configuration instead
of adding LLVM permanently to `~/.zshrc` or `~/.zshenv`:

```sh
LLVM_PREFIX="/opt/homebrew/opt/llvm"
PATH="$LLVM_PREFIX/bin:$PATH" cmake -S . -B build \
  -DMLIR_DIR="$LLVM_PREFIX/lib/cmake/mlir"
PATH="$LLVM_PREFIX/bin:$PATH" cmake --build build
PATH="$LLVM_PREFIX/bin:$PATH" ctest --test-dir build --output-on-failure
```

Notes:

- Do not put LLVM settings in `~/.zshenv`; that file affects scripts and tools
  launched outside your interactive terminal.
- You usually do not need Homebrew's `LDFLAGS` or `CPPFLAGS` for this project.
  CMake needs the MLIR package path, and the tests/native driver need LLVM tools
  such as `mlir-opt`, `llvm-as`, and `llc`.
- Reconfigure CMake after changing LLVM installs so cached tool paths are
  refreshed.

### Ubuntu 24.04

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential clang graphviz
sudo apt-get install -y libmlir-18-dev mlir-18-tools lld-18

cmake -S . -B build -DMLIR_DIR=/usr/lib/llvm-18/lib/cmake/mlir
cmake --build build
ctest --test-dir build --output-on-failure
```

## Compiler Pipeline

```text
source
  -> lexer
  -> parser
  -> AST
  -> semantic analysis
  -> typed IR
  -> MLIR
  -> LLVM IR
  -> native executable
```

## Repository Layout

```text
include/   C++ headers
src/       compiler implementation
runtime/   bootstrap runtime linked into native executables
examples/  nex example programs
tests/     CTest fixtures and golden outputs
docs/      reference docs, design notes, tutorials, backlog
```

## Documentation

- [`docs/reference/`](./docs/reference/README.md): current language, toolchain,
  and runtime behavior
- [`docs/IMPLEMENTATION_BACKLOG.md`](./docs/IMPLEMENTATION_BACKLOG.md): ordered
  implementation milestones
- [`NEXC_HOLY_BOOK.md`](./NEXC_HOLY_BOOK.md): educational explanation of the
  compiler internals
- [`LLM_REFERENCE.md`](./LLM_REFERENCE.md): short index for future chats/tools
- [`nex.md`](./nex.md): longer project and language direction

## License

Released under the [MIT License](./LICENSE).