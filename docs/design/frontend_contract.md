# NEX Frontend Contract

This document defines the first implementation contract for the hand-written
NEX frontend. It is intentionally smaller and more mechanical than the language
vision in `nex.md`: the goal is to make the lexer, parser, AST, diagnostics,
and first validation tests straightforward to build and review.

`docs/language/core_v0.md` is the normative language target for this contract.
Future features described in `nex.md` are out of scope until they receive their
own language specification.

## 1. Frontend Stages

The first frontend should keep these boundaries clear:

```text
source file
  -> lexer
  -> token stream
  -> parser
  -> AST
  -> semantic analysis
```

- The lexer recognizes token boundaries, keywords, comments, literals,
operators, punctuation, and source spans.
- The parser recognizes syntactic structure: declarations, statements, type
syntax, and expressions.
- Semantic analysis checks meaning: name resolution, type checking, mutability,
return rules, constant evaluation, and whether call statements may discard
their result.

This split matters because many programs are syntactically valid but
semantically invalid. For example, `return true;` can parse inside an `i32`
function, but semantic analysis must reject the type mismatch.

## 2. Locked Core v0 Choices

These choices narrow the implementation without closing off obvious future
extensions.

### 2.1 Integer Literals

Core v0 supports:

- decimal integer literals: `0`, `42`, `1234`
- hexadecimal integer literals with `0x` or `0X`: `0x2a`, `0XFF`

The lexer should preserve the raw spelling of the literal. It may also record
the base, but it should not decide the final integer type. Literal type
selection belongs to semantic analysis, where expected types and defaults are
known.

Rejected in Core v0:

- underscores: `1_000`
- suffixes: `42u32`
- binary/octal prefixes: `0b1010`, `0o755`
- invalid hex with no digits after the prefix: `0x`

These can be added later without changing the token or AST category.

### 2.2 Trailing Commas

Core v0 rejects trailing commas in function parameter lists and call argument
lists:

```nex
fn add(a: i32, b: i32,) -> i32 { return a + b; } // invalid in v0
foo(x, y,);                                      // invalid in v0
```

This keeps the first parser slightly simpler. Allowing trailing commas later is
a compatible extension.

### 2.3 `const`

Core v0 accepts `const` only at module scope:

```nex
const N: i32 = 4;
```

Inner scoped compile-time constants are reserved for a later revision. This
keeps the first constant evaluator and scoped symbol table smaller.

### 2.4 Expression Statements

The parser accepts call expressions as statements:

```nex
foo();
foo(1, 2);
```

The parser should reject arbitrary value expressions as statements:

```nex
1 + 2; // syntax error in v0
x;     // syntax error in v0
```

Whether a call statement is allowed to discard a value is a semantic rule. For
example, `print_i32(1);` is valid if `print_i32` returns `void`, while
`add(1, 2);` should be rejected or warned on if `add` returns `i32`, depending
on the semantic policy chosen later.

### 2.5 Type Names

The lexer reserves exactly the Core v0 keyword spellings from the language
specification. In particular:

- `bool` and `void` are keyword tokens.
- integer type names such as `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, and
`u64` lex as identifiers.

The parser recognizes those identifier spellings as built-in scalar types only
while parsing type syntax. This keeps the lexer independent of later user-defined
type names.

### 2.6 Non-ASCII Input

Core v0 syntax is ASCII-only. The lexer should:

- allow non-ASCII bytes inside comments
- reject non-ASCII outside comments with an unsupported-character diagnostic

String literals and Unicode identifiers are not part of Core v0.

## 3. Source Locations and Diagnostics

Every token and AST node should carry a source span.

```cpp
struct SourceSpan {
    uint32_t start;
    uint32_t end;
};
```

The span is a half-open byte range into the source buffer: `[start, end)`.
Line and column can be derived from the source buffer when reporting a
diagnostic. This keeps the core representation compact while still supporting
useful messages and caret diagnostics.

A diagnostic should include:

- severity: error, warning, or note
- message
- primary source span
- optional notes with their own spans

The first implementation may stop after the first hard error. Once the parser is
stable, targeted error recovery can be added around semicolons, braces, and item
boundaries.

## 4. Token Model

The token stream should include comments and whitespace only indirectly: they
advance source positions but do not produce parser-visible tokens.

Recommended token shape:

```cpp
enum class TokenKind {
    EndOfFile,
    Identifier,
    IntegerLiteral,

    KwBool,
    KwConst,
    KwElse,
    KwFalse,
    KwFn,
    KwIf,
    KwLet,
    KwMut,
    KwReturn,
    KwTrue,
    KwVoid,
    KwWhile,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AmpAmp,
    PipePipe,
    Arrow,

    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Colon,
};

struct Token {
    TokenKind kind;
    SourceSpan span;
};
```

The lexer can recover text for identifiers and literals by slicing the source
buffer using the span. If later performance or ownership concerns require it,
`Token` can gain an interned identifier handle or literal metadata.

### 4.1 Lexing Rules

The lexer should use maximal munch for multi-character operators:

- `==`, `!=`, `<=`, `>=`, `&&`, `||`, `->`

Identifier lexing should scan `[a-zA-Z_][a-zA-Z0-9_]*` and then check a keyword
table. This ensures `letx` is an identifier, not `let` followed by `x`.

Line comments start with `//` and continue until newline or end of file. Block
comments start with `/*`, end at the first `*/`, and do not nest. Unterminated
block comments are lexer errors.

## 5. AST Model

The first AST should be explicit and easy to dump. It does not need to be clever.

Recommended top-level shape:

```text
TranslationUnit
  Item*

Item
  FunctionDecl
  ConstDecl
```

Recommended statement family:

```text
Stmt
  BlockStmt
  LetStmt
  AssignStmt
  ReturnStmt
  IfStmt
  WhileStmt
  CallStmt
```

Recommended expression family:

```text
Expr
  IntegerLiteralExpr
  BoolLiteralExpr
  NameExpr
  CallExpr
  UnaryExpr
  BinaryExpr
  ParenExpr
```

Recommended type family:

```text
TypeSyntax
  BuiltinType
```

Each AST node should carry the span of the syntax that produced it. For nodes
with child nodes, the span should normally cover the full construct.

### 5.1 Ownership

Use simple owning storage in the AST. A good initial C++ shape is:

- `std::unique_ptr<T>` for single child nodes
- `std::vector<std::unique_ptr<T>>` for lists of polymorphic nodes
- `std::vector<T>` for value-like records such as parameters

Arena allocation can be introduced later if AST ownership noise becomes a
problem. Starting with `unique_ptr` keeps lifetime behavior explicit and
debuggable.

## 6. Parser Contract

Use recursive descent for declarations, statements, and type syntax. Use Pratt
parsing or precedence climbing for expressions.

Recommended parser entry points:

```text
parseTranslationUnit()
parseItem()
parseFunctionDecl()
parseConstDecl()
parseParameterList()
parseType()
parseBlockStmt()
parseStmt()
parseLetStmt()
parseReturnStmt()
parseIfStmt()
parseWhileStmt()
parseAssignmentOrCallStmt()
parseExpr()
```

`parseAssignmentOrCallStmt()` handles the only ambiguous statement starts in v0:
an identifier can begin either `x = expr;` or `foo(args);`.

### 6.1 Expression Precedence

Use the Core v0 precedence table:

```text
postfix call
unary - !
* / %
+ -
< > <= >=
== !=
&&
||
```

All binary operators in Core v0 are left-associative. Unary operators bind more
tightly than binary operators. Function calls bind tighter than unary and binary
operators.

Parentheses create grouped expressions and should be represented either as
`ParenExpr` or by giving the inner expression the combined span. Keeping
`ParenExpr` initially makes AST dumps more transparent while learning.

### 6.2 Syntax Errors

Initial syntax diagnostics should prioritize clarity over recovery:

- expected token after a known prefix, such as `expected ')'`
- unexpected token at item or statement start
- invalid top-level statement, such as `let` at module scope
- invalid expression statement, such as `1 + 2;`
- unterminated block or parameter list

## 7. First Validation Loop

Implement `--dump-tokens` before parser work. It should print each token kind
and span. Identifier and literal tokens should also show their source spelling.

Implement `--dump-ast` as soon as the parser can build a translation unit. The
dump should be plain text and stable enough for golden tests.

### 7.1 Initial Valid Inputs

Use these Core v0 programs as first parser fixtures:

```nex
fn main() -> void {
    return;
}
```

```nex
fn main() -> i32 {
    return 0;
}
```

```nex
const N: i32 = 4;

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

```nex
fn count() -> void {
    let mut x: i32 = 0;
    while (x < 10) {
        x = x + 1;
    }
    return;
}
```

### 7.2 Initial Invalid Inputs

Use these as first diagnostic fixtures:

```nex
let x: i32 = 1;
```

```nex
fn f() -> void {
    1 + 2;
}
```

```nex
fn f(a: i32,) -> void {
    return;
}
```

```nex
const BAD: i32 = 0x;
```

```nex
fn f() -> void {
    const X: i32 = 1;
}
```

### 7.3 Lexer Edge Cases

The first lexer tests should cover:

- comments: `//`, `/* */`, and unterminated `/*`
- keyword boundaries: `let`, `letx`, `_let`
- multi-character operators: `==`, `!=`, `<=`, `>=`, `&&`, `||`, `->`
- integer literals: `0`, `123`, `0x2a`, `0XFF`, invalid `0x`
- unsupported characters outside comments

## 8. Implementation Order

1. Source buffer, spans, diagnostics.
2. Token kinds and lexer.
3. `--dump-tokens`.
4. AST node definitions and AST dump format.
5. Parser for items, types, blocks, and statements.
6. Pratt or precedence-climbing expression parser.
7. `--dump-ast`.
8. Focused frontend tests from this contract and Core v0 examples.

