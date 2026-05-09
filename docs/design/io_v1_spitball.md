# Nex I/O v1 Spitball

This is a design note, not a finished specification. Core v0 proved that the
compiler can build native executables and print stdout. The next I/O work should
avoid blindly copying C `stdio`, Rust traits, or shell syntax without deciding
what Nex wants I/O to feel like.

## Current Implemented Slice

The compiler currently supports:

```nex
print("text");
println("text");
println(readln());
```

`readln() -> str` is intentionally tiny. It reads one line from stdin, strips the
line ending, and returns a temporary `str` backed by the bootstrap runtime. The
first intended use is direct piping into output:

```nex
fn main() -> void {
    println(readln());
    return;
}
```

This does not solve owned strings, heap allocation, arrays, slices, or file
handles. It gives us a real input experiment while those larger pieces are still
being designed.

## Design Pressure

I/O should make cost and effects visible:

- Reading may block.
- Writing may fail.
- Files and pipes are resources, not magic globals.
- Text has encoding and line-ending questions.
- Buffers allocate or borrow memory, and Nex should not hide that accidentally.

## Direction A: Explicit Resources

This direction treats I/O endpoints as values:

```nex
let line: str = stdin.readln();
stdout.println(line);

let file = File.open("notes.txt", .read)?;
let text = file.read_to_string()?;
```

Good parts:

- Resource ownership is obvious.
- Blocking operations are attached to a source or sink.
- It can grow into explicit capabilities for sandboxing or realtime rules.

Risks:

- It may feel too object-method flavored if Nex wants a leaner systems style.
- It needs error/result syntax before files feel honest.

## Direction B: Pipeline Data Flow

This direction makes movement from source to transform to sink first-class:

```nex
stdin
    |> lines()
    |> take(10)
    |> stdout.write_lines();
```

Good parts:

- It naturally describes pipes and streaming transforms.
- It can support shell-like composition without becoming shell syntax.
- It is a good future fit for data processing and linear algebra pipelines.

Risks:

- Pipeline syntax can hide allocation or buffering unless effects are explicit.
- The compiler needs a clear story for lazy versus eager evaluation.

## Direction C: Streams And Iteration

This direction says inputs are streams of items:

```nex
for line in stdin.lines() {
    println(line);
}
```

Good parts:

- It scales from stdin to files to sockets.
- It is easy to reason about line-by-line memory use.
- It points toward future iterator abstractions.

Risks:

- Nex does not have `for`, iterators, option/result, or owned buffers yet.
- It depends on array/vector/string decisions.

## Recommended Near-Term Path

1. Keep `readln() -> str` as the experimental stdin foothold.
2. Do not add file I/O until error handling and string ownership are clearer.
3. Prefer explicit resource vocabulary for files: open, read, write, close/drop.
4. Revisit pipeline syntax after streams/iterators exist, not before.

The likely next real feature after `readln()` is not “all files.” It is a small
resource/error design that lets file operations be honest about failure.
