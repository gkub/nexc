# nex Core v0 Tutorial

This is the beginner-facing guide for writing the nex that exists today.

Core v0 is small on purpose. You can write scalar functions, constants,
variables, arithmetic, `if` / `else`, `while` loops, string literals, and
`print` / `println` calls. You cannot allocate memory, use arrays, import files,
spawn tasks, or do general file/pipe I/O yet.

The current compiler frontend can **check** nex programs:

```sh
./nexc.sh check-file examples/add.nexs
```

If the command exits successfully, the file has valid Core v0 syntax and passes
the current semantic checks.

## Hello, World

nex has built-in `print` and `println` calls in the frontend:

```c
fn main() -> void {
    println("Hello, world!");
    return;
}
```

Check it:

```sh
./nexc.sh check-file examples/hello.nexs
```

Compile and run it directly with `build/nexc`:

```sh
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

The executable prints `Hello, world!`.

## File Shape

A `.nexs` file contains top-level declarations:

```c
const ANSWER: i32 = 42;

fn main() -> i32 {
    return ANSWER;
}
```

Top-level statements are not allowed:

```c
let x: i32 = 1; // invalid at top level
```

Put executable code inside functions.

## Functions

Functions use this form:

```c
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

The return type is required. Use `void` when a function returns no value:

```c
fn do_nothing() -> void {
    return;
}
```

An executable program uses:

```c
fn main() -> void {
    return;
}
```

or:

```c
fn main() -> i32 {
    return 0;
}
```

In Core v0, `main` must not have parameters.

## Types

Core v0 has fixed-width integer types:

```text
i8   i16   i32   i64
u8   u16   u32   u64
```

Signed types (`i*`) can represent negative and positive values. Unsigned types
(`u*`) represent only zero and positive values.

Core v0 also has:

```text
bool
str
void
```

`bool` values are:

```c
true
false
```

`void` is only used as a function return type.

`str` is the type of a string literal:

```c
"Hello, world!"
```

Core v0 string literals lower to immutable bytes plus an explicit byte length.
That lets `print` and `println` write the exact bytes without relying on a
C-style trailing `\0` terminator.

## Constants

Use `const` for module-level compile-time constants:

```c
const LIMIT: i32 = 10;

fn main() -> i32 {
    return LIMIT;
}
```

A `const` initializer must be compile-time evaluable:

```c
const BAD: i32 = add(1, 2); // invalid: function calls are not const yet
```

The compiler checks literal ranges:

```c
const BAD: i8 = 128; // invalid: i8 max is 127
```

## Local Variables

Use `let` inside functions:

```c
fn main() -> i32 {
    let x: i32 = 1;
    return x;
}
```

`let` bindings are immutable by default:

```c
fn main() -> i32 {
    let x: i32 = 1;
    x = 2; // invalid
    return x;
}
```

Use `let mut` when a variable needs to change:

```c
fn main() -> i32 {
    let mut x: i32 = 1;
    x = x + 1;
    return x;
}
```

Every `let` currently needs an initializer.

## Arithmetic

Core v0 supports:

```text
+  -  *  /  %
```

Example:

```c
fn square(x: i32) -> i32 {
    return x * x;
}
```

Integer overflow is defined at runtime as wrapping, but constant expressions
that overflow are diagnosed:

```c
const BAD: i8 = 100 + 30; // invalid: overflows i8
```

Division by zero in a constant expression is also diagnosed:

```c
const BAD: i32 = 10 / 0; // invalid
```

## Comparisons and Booleans

Comparisons produce `bool`:

```c
fn is_positive(x: i32) -> bool {
    return x > 0;
}
```

Supported comparison operators:

```text
==  !=  <  <=  >  >=
```

Logical operators:

```text
!   &&   ||
```

Current Core v0 `&&` and `||` are eager: both operands are evaluated before the
boolean result is computed. Do not rely on short-circuit behavior yet.

Conditions accept `bool` or integer values. For integers, zero is false and
nonzero is true, like C:

```c
fn truthy(x: i32) -> i32 {
    if (x) {
        return 1;
    } else {
        return 0;
    }
}
```

## If / Else

Use parentheses around conditions:

```c
fn abs_i32(x: i32) -> i32 {
    if (x < 0) {
        return -x;
    } else {
        return x;
    }
}
```

For non-`void` functions, all paths must return:

```c
fn bad(x: i32) -> i32 {
    if (x > 0) {
        return x;
    }
    // invalid: what happens when x <= 0?
}
```

## While Loops

Use `while` for loops:

```c
fn count_to_ten() -> i32 {
    let mut x: i32 = 0;

    while (x < 10) {
        x = x + 1;
    }

    return x;
}
```

The current return checker does not assume a `while` loop runs, even if the
condition looks constant. Return after the loop if the function returns a value.

## Function Calls

Call functions by name:

```c
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    return add(40, 2);
}
```

Argument count and types are checked.

Only `void` calls may be used as standalone statements:

```c
fn returns_value() -> i32 {
    return 1;
}

fn main() -> void {
    returns_value(); // invalid: result is discarded
    return;
}
```

## Printing

Core v0 has two built-in functions:

```c
print("text");
println("text");
println(readln());
```

`print` and `println` take one `str` argument and return `void`. `readln()` reads
one stdin line and returns a temporary `str` without the line ending.

```c
fn main() -> void {
    print("Hello, ");
    println("world!");
    return;
}
```

This is intentionally simple. nex does not have Python-style f-strings yet,
because interpolation and formatting can hide allocation, conversion, and
runtime work. A future formatting design should make those costs explicit.

## Reading One Line

The first input slice is deliberately small:

```c
fn main() -> void {
    println(readln());
    return;
}
```

Compile and run it:

```sh
build/nexc examples/stdin_echo.nexs -o build/stdin_echo
printf 'hello\n' | build/stdin_echo
```

For now, use the returned string directly. Storing input strings in variables is
waiting on a fuller string ownership and buffer model.

## Reading Numbers And Booleans

You can parse text input into typed values with explicit helpers:

```c
fn main() -> i32 {
    let value: i32 = parse_i32(readln());
    if (input_ok()) {
        return value;
    } else {
        return 0 - 1;
    }
}
```

Current parse helpers:

- `parse_i32(str) -> i32`
- `parse_u64(str) -> u64`
- `parse_bool(str) -> bool` (`true`, `false`, `1`, `0`)

Use `input_ok()` after `readln()`/parse calls to check success.

## Checking Your Code

Use:

```sh
./nexc.sh check-file path/to/file.nexs
```

Useful debugging commands:

```sh
./nexc.sh tokens path/to/file.nexs
./nexc.sh ast path/to/file.nexs
./nexc.sh ast-graph path/to/file.nexs
./nexc.sh mlir path/to/file.nexs
./nexc.sh llvm path/to/file.nexs
```

`ast-graph` writes `ast.dot` and `ast.svg` if Graphviz is installed.

## Common Errors

Wrong return type:

```text
error: cannot return value of type `bool` from function returning `i32`
```

Immutable assignment:

```text
error: cannot assign to immutable binding `x`
```

Missing return path:

```text
error: not all paths in function `main` return a value of type `i32`
```

Out-of-range literal:

```text
error: integer literal `128` does not fit in type `i8`
```

## What Comes Later

Core v0 does not yet include:

- formatted strings or interpolation
- printing values other than `str`
- arrays or indexing
- user-defined types
- imports/modules
- file reading/writing and pipe abstractions
- allocation, buffers, channels, tasks, or realtime regions

Those belong to later language/compiler phases.
