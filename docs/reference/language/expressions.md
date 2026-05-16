# Expressions

## Table of Contents

- [Summary](#summary)
- [Syntax](#syntax)
- [Expression Forms](#expression-forms)
- [Operator Precedence](#operator-precedence)
- [Logical AND/OR (`&&`, `||`)](#logical-andor--)
- [Arrays And Indexing](#arrays-and-indexing)
- [Type Rules](#type-rules)
- [Examples](#examples)
- [See Also](#see-also)

## Summary

Expressions produce values. nex currently supports literals, names, calls, unary
and binary operators, parenthesized expressions, array literals, and indexing.

## Syntax

```text
expression ::= literal
             | name
             | call-expression
             | unary-expression
             | binary-expression
             | "(" expression ")"
             | array-literal
             | index-expression

call-expression  ::= name "(" arguments? ")"
array-literal    ::= "[" expression ("," expression)* "]"
index-expression ::= expression "[" expression "]"
```

## Expression Forms

| Form | Example | Notes |
| ---- | ------- | ----- |
| integer literal | `42`, `0xff` | Checked against the expected integer type. |
| boolean literal | `true`, `false` | Type is `bool`. |
| string literal | `"hello"` | Type is `str`. |
| name reference | `count` | Resolves to a local, parameter, function, or module constant. |
| function call | `add(a, b)` | Arguments are checked against the callee signature. |
| unary operator | `-x`, `!ok` | `-` negates integers; `!` accepts `bool` or integer condition values. |
| binary operator | `a + b`, `x == y` | Arithmetic, comparison, equality, and logical operators. |
| parenthesized expression | `(a + b) * c` | Overrides default precedence. |
| array literal | `[1, 2, 3]`, `[[1, 2], [3, 4]]` | Length and nested shape must match the contextual fixed-array type, or element types/shapes must agree for inference. |
| indexing | `items[i]`, `grid[i][j]` | Base expression must have fixed array type. |

## Operator Precedence

From highest to lowest:

| Level | Operators / forms | Associativity |
| ----- | ----------------- | ------------- |
| 1 | calls `f(args)`, indexing `a[i]` | left |
| 2 | unary `-`, `!` | right |
| 3 | `*`, `/`, `%` | left |
| 4 | `+`, `-` | left |
| 5 | `<`, `>`, `<=`, `>=` | left |
| 6 | `==`, `!=` | left |
| 7 | `&&` | left |
| 8 | logical or | left |

## Logical AND/OR (`&&`, `||`)

`&&` and `||` use **short-circuit** evaluation, matching familiar C-family rules:

- For `a && b`, `b` is evaluated only if `a` is true after applying condition rules.
- For `a || b`, `b` is evaluated only if `a` is false.

Any condition context accepts operands that are `bool` or an integer type. As
with `if` and `while`, integer **zero** is false and any non-zero integer is
true.

Module-level `const` initializers are a special case: they must be compile-time
expressions, but the compiler still types and analyzes both operands. The
intermediate typed IR for `const` chooses a **linear**, truthify-then-`Binary`
representation so constant lowering stays a simple instruction list; the
**language** nevertheless follows short-circuit rules for constant folding (for
example, `false && <anything>` is a constant `false` without requiring a constant
right-hand side).

Example where short-circuit avoids evaluating the division when `y == 0`:

```nex
if (y != 0 && x / y > 1) {
    return 1;
} else {
    return 0;
}
```

## Arrays And Indexing

- An array **literal** lists element expressions separated by commas inside `[` `]`.
- Nested array literals are written by nesting array literals, for example
  `[[1, 2], [3, 4]]`.
- An array **load** is postfix: the base expression must have a fixed array type;
  the index must be an integer type (`i32` is typical).
- Chained indexing walks nested arrays one dimension at a time: `grid[1][0]`.
- Assignment to `name[index]` or `name[index][...]` requires `let mut` on the
  root array binding.

## Type Rules

- arithmetic operators currently require integer operands
- comparison/equality produce `bool`
- `!` accepts `bool` or integer-like condition values
- call argument count/types are checked against resolved callee signature

## Examples

```nex
fn main() -> i32 {
    let x: i32 = 10;
    let y: i32 = 2;
    if ((x / y) > 3 && (x % y) == 0) {
        return 1;
    } else {
        return 0;
    }
}
```

## See Also

- [types.md](types.md)
- [statements.md](statements.md)
- [functions_and_calls.md](functions_and_calls.md)
- [builtins_and_io.md](builtins_and_io.md)
