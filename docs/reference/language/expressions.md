# Expressions

## Table of Contents

- [Summary](#summary)
- [Syntax](#syntax)
- [Expression Forms](#expression-forms)
- [Operator Precedence](#operator-precedence)
- [Logical Operators Note](#logical-operators-note)
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
| array literal | `[1, 2, 3]` | Length must match the contextual `[T; N]` type, or element types must agree for inference. |
| indexing | `items[i]` | Base expression must have fixed array type. |

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

## Logical Operators Note

Current `&&` and `||` semantics are eager:

- both operands are evaluated
- then the boolean operation is applied

They do not short-circuit yet. For example, `y != 0 && x / y > 1` still evaluates
`x / y`.

## Arrays And Indexing

- An array **literal** lists element expressions separated by commas inside `[` `]`.
- An array **load** is postfix: the base expression must have a fixed array type;
  the index must be an integer type (`i32` is typical).
- Assignment to `name[index]` requires `let mut` on the array binding.

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
