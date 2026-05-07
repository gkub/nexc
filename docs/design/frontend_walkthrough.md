# Frontend Code Walkthrough

This document explains the current NEX frontend implementation at the code
level. It is intended to make the compiler easier to review, teach, and extend
as the project grows.

The current frontend is intentionally small:

```text
source text -> SourceFile -> Lexer -> tokens -> Parser -> AST
```

Semantic analysis, MLIR generation, LLVM lowering, and native code generation
are not implemented yet.

## 1. Build Shape

The root `CMakeLists.txt` defines two targets:

- `nexc_frontend`: a library containing reusable frontend code
- `nexc`: a small command-line executable that uses the frontend library

The frontend library lives under:

```text
include/nexc/frontend/
src/frontend/
```

The CLI lives under:

```text
src/tools/nexc/main.cpp
```

This split matters because the lexer and parser should eventually be reusable by
tests, editor tooling, formatters, and future compiler drivers. Keeping the CLI
thin prevents compiler logic from getting trapped in `main()`.

## 2. Source Files and Spans

Files:

```text
include/nexc/frontend/source.h
src/frontend/source.cpp
```

`SourceFile` owns the source path and source text. It also records the byte
offset where each line starts.

`SourceSpan` is a half-open byte range:

```cpp
struct SourceSpan {
    std::size_t start = 0;
    std::size_t end = 0;
};
```

The range `[start, end)` means:

- `start` is included
- `end` is excluded

For example, in this source:

```nex
fn main() -> void {
```

the token `fn` might have span `[0, 2)`.

Spans are one of the most important pieces of compiler infrastructure. They let
later code answer questions such as:

- Which source text produced this token?
- Where should an error message point?
- Which AST node caused this diagnostic?

The compiler stores byte offsets internally and converts to line/column only
when printing diagnostics. That keeps the core representation simple and avoids
updating line/column state everywhere in the parser.

## 3. Diagnostics

Files:

```text
include/nexc/frontend/diagnostic.h
src/frontend/diagnostic.cpp
```

A diagnostic is a compiler message tied to source text:

```cpp
struct Diagnostic {
    DiagnosticSeverity severity;
    std::string message;
    SourceSpan span;
};
```

`DiagnosticBag` collects diagnostics during lexing and parsing.

The frontend does not throw exceptions for normal user mistakes like missing
semicolons. Those are expected compiler errors, so they are collected as
diagnostics. Exceptions are reserved for operational problems, such as failing to
open a file.

Current diagnostic printing is intentionally basic:

```text
path/to/file.nexs:line:column: error: message
```

Caret rendering is a natural next improvement.

## 4. Tokens

Files:

```text
include/nexc/frontend/token.h
src/frontend/token.cpp
```

The lexer turns characters into tokens. A token is a classified piece of source:

```cpp
struct Token {
    TokenKind kind;
    SourceSpan span;
};
```

For example:

```nex
fn add(a: i32, b: i32) -> i32
```

becomes a stream shaped like:

```text
KwFn
Identifier(add)
LeftParen
Identifier(a)
Colon
Identifier(i32)
Comma
Identifier(b)
Colon
Identifier(i32)
RightParen
Arrow
Identifier(i32)
```

The token stream is flat. It does not yet know that `add` is a function name or
that `a: i32` is a parameter. That structure appears later in the parser.

### Keywords vs Identifiers

The lexer first scans the longest identifier-like spelling. Then it checks
whether that spelling is reserved.

This matters for examples like:

```nex
letx
```

`letx` is one identifier, not the keyword `let` followed by identifier `x`.
That rule is commonly called maximal munch.

Integer type names such as `i32` and `u64` currently lex as identifiers. The
parser recognizes them as built-in types only when parsing type syntax.

## 5. Lexer

Files:

```text
include/nexc/frontend/lexer.h
src/frontend/lexer.cpp
```

The lexer consumes raw source text and produces `std::vector<Token>`.

Its responsibilities are narrow:

- skip whitespace
- skip comments
- recognize keywords
- recognize identifiers
- recognize integer literals
- recognize operators and punctuation
- attach spans
- report invalid characters and malformed literals

The lexer does not know grammar. It does not know whether a token is in a valid
position. For example, the lexer will happily produce a `KwReturn` token at the
top level. The parser decides whether that token is syntactically legal there.

### Cursor Helpers

The lexer has a byte cursor named `current_`.

Important helpers:

- `peek()` looks at the current character without consuming it.
- `peek(1)` looks one character ahead.
- `advance()` consumes one character.
- `match(expected)` consumes the next character only if it matches.

These helpers make multi-character tokens straightforward:

```text
-  -> Minus
-> -> Arrow
=  -> Equal
== -> EqualEqual
```

### Trivia

Whitespace and comments are often called trivia in compiler code. They matter
for source positions but do not matter to the grammar.

Core v0 skips trivia instead of creating parser-visible tokens for it:

```nex
fn main() -> void {
    // comment
    return;
}
```

The parser sees `fn`, `main`, `(`, `)`, `->`, `void`, `{`, `return`, `;`, `}`.
It does not see the spaces, newline characters, or comment.

### Literals

The lexer validates the surface form of integer literals:

- decimal: `0`, `42`
- hex: `0x2a`, `0XFF`

It preserves the raw spelling. It does not decide whether a literal is `i32`,
`u64`, or some other type. Literal typing needs context and belongs to semantic
analysis.

## 6. AST

Files:

```text
include/nexc/frontend/ast.h
src/frontend/ast.cpp
```

AST means abstract syntax tree. The token stream is flat, but programs are
nested.

For example:

```nex
return a + b * c;
```

has a tree shape like:

```text
ReturnStmt
  BinaryExpr Plus
    NameExpr a
    BinaryExpr Star
      NameExpr b
      NameExpr c
```

That tree captures precedence. `b * c` is nested under `a + ...`, so later
compiler stages know multiplication happens before addition.

The AST has four broad families:

- `TranslationUnit`: the whole source file
- `Item`: top-level declarations such as functions and constants
- `Stmt`: statements inside function bodies
- `Expr`: expressions that produce values

The current AST uses `std::unique_ptr` for owned child nodes. This makes
ownership explicit and easy to debug. Arena allocation can be introduced later
if AST allocation becomes noisy or performance-sensitive.

## 7. Parser

Files:

```text
include/nexc/frontend/parser.h
src/frontend/parser.cpp
```

The parser consumes tokens and produces an AST.

The parser checks syntax, not meaning. It can tell that this has valid grammar:

```nex
fn f() -> i32 {
    return true;
}
```

It cannot decide whether `true` is a valid return value for an `i32` function.
That requires type information and belongs to semantic analysis.

### Recursive Descent

Most parser functions are recursive descent functions. Each function parses one
kind of syntax:

```text
parseTranslationUnit()
parseItem()
parseFunctionDecl()
parseConstDecl()
parseBlockStmt()
parseStmt()
parseLetStmt()
parseReturnStmt()
parseIfStmt()
parseWhileStmt()
```

This style is useful for an educational compiler because parser code maps
directly to the grammar. A function declaration in the grammar becomes
`parseFunctionDecl()` in the code.

### Token Cursor

The parser has a token cursor named `current_`.

Important helpers:

- `peek()` looks at the current token.
- `peek(1)` looks one token ahead.
- `check(kind)` asks whether the current token has a kind.
- `match(kind)` consumes the current token if it has that kind.
- `expect(kind, message)` requires a token and reports an error if it is absent.

`expect()` is the main syntax-error helper. It emits a diagnostic and returns a
placeholder token so the parser can build a partial tree instead of immediately
crashing.

### Statement Ambiguity

In Core v0, a statement beginning with an identifier can mean either assignment
or call statement:

```nex
x = x + 1;
foo(x);
```

The parser resolves this with one token of lookahead:

- identifier followed by `=` means assignment
- otherwise, parse an expression and require it to be a call expression

This will need to evolve when the language adds richer assignment targets such
as indexing or field access.

### Expression Parsing

Expressions use precedence climbing.

The parser must make this:

```nex
1 + 2 * 3
```

mean this:

```text
1 + (2 * 3)
```

not this:

```text
(1 + 2) * 3
```

`parseExpr(minPrecedence)` handles binary operators by comparing the next
operator's precedence against the minimum precedence required at the current
recursion level.

Core v0 binary operators are left-associative. That means:

```nex
a - b - c
```

parses as:

```text
(a - b) - c
```

Calls are parsed as postfix expressions so they bind very tightly:

```nex
square(7) + 1
```

becomes:

```text
BinaryExpr Plus
  CallExpr square(7)
  IntegerLiteral 1
```

## 8. CLI

File:

```text
src/tools/nexc/main.cpp
```

The CLI currently supports two inspection modes:

```sh
build/nexc --dump-tokens examples/minimal.nexs
build/nexc --dump-ast examples/add.nexs
```

Both modes lex the file first. `--dump-tokens` prints the token stream and stops.
`--dump-ast` passes the token stream into the parser and prints the resulting
tree.

The CLI is intentionally thin. The compiler logic should live in libraries so it
can be reused by tests and future tools.

## 9. Tests and Examples

Examples live under:

```text
examples/
```

Current examples:

- `minimal.nexs`
- `add.nexs`
- `control_flow.nexs`

Invalid parser fixtures live under:

```text
tests/
```

Current invalid fixtures check:

- trailing commas are rejected
- inner `const` is rejected
- arbitrary expression statements are rejected

CTest currently verifies that valid examples succeed and invalid fixtures fail.
The next testing improvement should be golden output tests for `--dump-tokens`
and `--dump-ast`.

## 10. Current Limitations

The current frontend does not yet implement:

- symbol tables
- duplicate-name checks
- type checking
- mutability checking
- function return checking
- `main` signature validation
- constant evaluation
- MLIR generation
- LLVM lowering
- native code generation
- Graphviz AST output

Those are later stages. The current project state is a syntax frontend, not a
full compiler.

## 11. Recommended Next Steps

The safest next steps are:

1. Add caret diagnostics with source-line printing.
2. Add golden tests for token and AST output.
3. Implement semantic analysis for names, scopes, types, mutability, and return
   rules.
4. Add constant evaluation for module-level `const`.
5. Add a small typed intermediate representation or direct MLIR generation once
   semantic analysis is trustworthy.

MLIR and LLVM should come after semantic analysis. Lowering invalid or untyped
syntax into IR makes the backend harder to debug because frontend mistakes
become backend confusion.
