# Language Quick Reference

Copy-pasteable shapes for the language surface implemented today. For exact
rules, follow the linked topic pages.

## Declarations

```nex
const LIMIT: i32 = 10;
const PAIR: [i32; 2] = [1, 2];

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    return add(LIMIT, PAIR[1]);
}
```

- Top-level declarations are `fn` and `const`.
- Executable `main` must have no parameters and return `void` or `i32`.

See [declarations_and_modules.md](declarations_and_modules.md) and
[functions_and_calls.md](functions_and_calls.md).

## Types

```text
i8 i16 i32 i64
u8 u16 u32 u64
f32 f64
bool
str
void
[T; N]
```

Examples:

```nex
let n: i32 = 42;
let ratio: f64 = 3.5 / 2.0;
let sample: f32 = 1.25;
let ok: bool = true;
let xs: [i32; 3] = [1, 2, 3];
let grid: [[i32; 2]; 2] = [[1, 2], [3, 4]];
```

See [types.md](types.md).

## Locals and Assignment

```nex
let x: i32 = 1;
let mut y: i32 = 0;
y = y + 1;

let mut late: i32;
late = 5;

let mut a: [i32; 2];
a[0] = 10;
a[1] = 20;
```

- `let` bindings are immutable.
- `let mut name: T;` is allowed when definite-assignment rules prove writes
  happen before reads.
- Array element assignment requires a mutable root binding.

See [statements.md](statements.md) and
[../../design/definite_assignment.md](../../design/definite_assignment.md).

## Expressions

```nex
let z: i32 = (a + b) * 2;
let f: f64 = (3.0 + 4.0) / 2.0;
let same: bool = z == 10;
let guarded: bool = denom != 0 && z / denom > 1;
let item: i32 = grid[1][0];
```

Common forms:

```text
name
callee(arg1, arg2)
-x
!condition
a + b
a == b
a && b
a || b
[expr, expr]
base[index]
```

See [expressions.md](expressions.md).

## Statements

```nex
if (x > 0) {
    println("positive");
} else {
    println("zero or negative");
}

while (x < 10) {
    x = x + 1;
}

for (let mut i: i32 = 0; i < 10; i = i + 1) {
    if (i == 5) {
        continue;
    }
}

return x;
```

See [statements.md](statements.md).

## Built-ins

```text
print(fmt: str, ...) -> void
println(fmt: str, ...) -> void
readln() -> str
parse_i32(text: str) -> i32
parse_u64(text: str) -> u64
parse_bool(text: str) -> bool
input_ok() -> bool
```

Example:

```nex
fn main() -> i32 {
    print("value: ");
    let value: i32 = parse_i32(readln());
    if (input_ok()) {
        println("parsed {}", value);
        return value;
    }
    return 0 - 1;
}
```

Float formatting supports fixed decimal precision:

```nex
println("pi-ish={:.2}", 3.14159);
```

See [builtins_and_io.md](builtins_and_io.md).

## Current Gaps

For a quick implemented/gaps inventory, see
[feature_inventory.md](feature_inventory.md). Implementation sequencing lives in
[docs/IMPLEMENTATION_BACKLOG.md](../../IMPLEMENTATION_BACKLOG.md).
