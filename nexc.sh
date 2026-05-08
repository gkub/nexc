#!/usr/bin/env bash

# Developer convenience wrapper for the nex compiler project.
#
# CMake and CTest remain the real build/test tools. This script just packages
# the common commands behind names that are easy to remember while still
# printing the underlying command before running it.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$SCRIPT_DIR/build"}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-}"

usage() {
    cat <<'EOF'
usage: ./nexc.sh <command> [args]

Common commands:
  configure              Configure CMake into ./build
  build                  Build the compiler
  test                   Run CTest with failure output
  check                  Configure, build, and test
  clean                  Remove the build directory
  rebuild                Clean, configure, and build

Frontend inspection:
  tokens <file.nexs>     Build if needed, then run --dump-tokens
  ast <file.nexs>        Build if needed, then run --dump-ast
  ast-dot <file.nexs>    Build if needed, then run --dump-ast-dot
  ast-graph <file.nexs> [prefix]
                           Write Graphviz DOT and SVG files
  check-file <file.nexs> Build if needed, then run --check

Examples:
  ./nexc.sh check
  ./nexc.sh ast examples/add.nexs
  ./nexc.sh check-file examples/add.nexs
  ./nexc.sh ast-dot examples/add.nexs > ast.dot
  ./nexc.sh ast-graph examples/add.nexs
  ./nexc.sh ast-graph examples/add.nexs build/add_ast

Useful environment variables:
  BUILD_DIR=build-release ./nexc.sh check
      Use a different build directory.

  BUILD_TYPE=Release ./nexc.sh rebuild
      Configure a Release build instead of Debug.

  CMAKE_GENERATOR=Ninja ./nexc.sh configure
      Use a specific CMake generator.

Notes:
  This script intentionally wraps ordinary CMake/CTest commands. If something
  fails, the printed command is the command to inspect or rerun manually.
EOF
}

run() {
    printf '+' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    "$@"
}

cmake_configure_args() {
    printf '%s\0' cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    if [[ -n "$CMAKE_GENERATOR" ]]; then
        printf '%s\0' -G "$CMAKE_GENERATOR"
    fi
}

configure() {
    local -a args=()
    while IFS= read -r -d '' arg; do
        args+=("$arg")
    done < <(cmake_configure_args)
    run "${args[@]}"
}

build() {
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure
    fi
    run cmake --build "$BUILD_DIR" --parallel
}

test_all() {
    build
    run ctest --test-dir "$BUILD_DIR" --output-on-failure
}

compiler() {
    # Keep build chatter on stderr so commands such as
    # `./nexc.sh ast-dot file.nexs > ast.dot` produce a clean redirected DOT
    # file on stdout.
    build >&2
    local exe="$BUILD_DIR/nexc"
    if [[ ! -x "$exe" ]]; then
        echo "error: expected compiler executable at $exe" >&2
        exit 1
    fi
    run "$exe" "$@"
}

ast_graph() {
    if [[ $# -lt 1 || $# -gt 2 ]]; then
        echo "usage: ./nexc.sh ast-graph <file.nexs> [output-prefix]" >&2
        exit 2
    fi

    local input="$1"
    local prefix="${2:-ast}"
    local dot_file="${prefix}.dot"
    local svg_file="${prefix}.svg"
    local dot_dir
    dot_dir="$(dirname -- "$dot_file")"

    if [[ "$dot_dir" != "." && ! -d "$dot_dir" ]]; then
        echo "error: output directory does not exist: $dot_dir" >&2
        exit 1
    fi

    compiler --dump-ast-dot "$input" > "$dot_file"

    if ! command -v dot >/dev/null 2>&1; then
        echo "wrote $dot_file" >&2
        echo "error: Graphviz 'dot' command not found; install graphviz to render SVG" >&2
        exit 1
    fi

    run dot -Tsvg "$dot_file" -o "$svg_file"
    echo "wrote $dot_file" >&2
    echo "wrote $svg_file" >&2
}

if [[ $# -eq 0 ]]; then
    usage
    exit 2
fi

case "$1" in
    -h|--help|help)
        usage
        ;;
    configure)
        configure
        ;;
    build)
        build
        ;;
    test)
        test_all
        ;;
    check)
        configure
        build
        run ctest --test-dir "$BUILD_DIR" --output-on-failure
        ;;
    clean)
        run rm -rf "$BUILD_DIR"
        ;;
    rebuild)
        run rm -rf "$BUILD_DIR"
        configure
        build
        ;;
    tokens)
        if [[ $# -ne 2 ]]; then
            echo "usage: ./nexc.sh tokens <file.nexs>" >&2
            exit 2
        fi
        compiler --dump-tokens "$2"
        ;;
    ast)
        if [[ $# -ne 2 ]]; then
            echo "usage: ./nexc.sh ast <file.nexs>" >&2
            exit 2
        fi
        compiler --dump-ast "$2"
        ;;
    ast-dot)
        if [[ $# -ne 2 ]]; then
            echo "usage: ./nexc.sh ast-dot <file.nexs>" >&2
            exit 2
        fi
        compiler --dump-ast-dot "$2"
        ;;
    ast-graph)
        shift
        ast_graph "$@"
        ;;
    check-file)
        if [[ $# -ne 2 ]]; then
            echo "usage: ./nexc.sh check-file <file.nexs>" >&2
            exit 2
        fi
        compiler --check "$2"
        ;;
    *)
        echo "error: unknown command '$1'" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac
