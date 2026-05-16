# nexc

`nexc` is the reference compiler for **nex**. It is an educational C++ compiler
project with a hand-written frontend, typed IR, MLIR/LLVM lowering, and native
executable output for small programs.

## Status

Implemented today:

- lexer, parser, AST dumps, semantic checking
- typed IR, MLIR, and LLVM IR dumps
- native executable generation through `build/nexc <file.nexs> -o <output>` on
  configured Linux and macOS hosts
- runtime-backed `print` / `println` with `{}` formatting
- selected input helpers such as `readln`, `parse_i32`, `parse_u64`,
  `parse_bool`, and `input_ok`
- `if` / `else`, `while`, C-style `for`, `break`, and `continue`
- fixed-size arrays for locals, function ABI, module constants, nested literals,
  indexing, and mutable element assignment

See [`docs/reference/language`](./docs/reference/language/README.md) if you want to learn how to write nex code, and
[`NEXC_HOLY_BOOK.md`](./NEXC_HOLY_BOOK.md) for an educational tour of how the nex compiler works.

## Table of Contents

- [Quick Start](#quick-start)
- [Setup](#setup)
- [Compiler Pipeline](#compiler-pipeline)
- [Repository Layout](#repository-layout)
- [Key Documents](#key-documents)
- [Forward Priorities](#forward-priorities)
- [License](#license)

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

## Key Documents

- [`NEXC_HOLY_BOOK.md`](./NEXC_HOLY_BOOK.md): educational explanation of the
  compiler internals
- [`docs/reference/`](./docs/reference/README.md): current language, toolchain,
  and runtime behavior
- [`docs/reference/language/`](./docs/reference/language/README.md): language
  rules by topic
- [`docs/reference/toolchain/`](./docs/reference/toolchain/README.md): CLI,
  build, tests, diagnostics, and artifacts
- [`docs/IMPLEMENTATION_BACKLOG.md`](./docs/IMPLEMENTATION_BACKLOG.md): ordered
  implementation milestones
- [`LLM_REFERENCE.md`](./LLM_REFERENCE.md): short index for future chats/tools
- [`nex.md`](./nex.md): longer project and language direction

## Forward Priorities

Near-term work is tracked in [`docs/IMPLEMENTATION_BACKLOG.md`](./docs/IMPLEMENTATION_BACKLOG.md).
That file is the source of truth for implementation order.

## License

Released under the [MIT License](./LICENSE).
