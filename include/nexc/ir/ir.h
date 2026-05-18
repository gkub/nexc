#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/frontend/source.h"
#include "nexc/frontend/token.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nexc::ir {

// IR Type is the backend-facing version of a nex type.
//
// Scalar types use an empty `arrayDimensions`. Fixed-size arrays store outer-to-inner
// dimension lengths in `arrayDimensions` and the leaf scalar in `kind`.
struct Type {
    BuiltinTypeKind kind = BuiltinTypeKind::Invalid;
    std::vector<std::uint64_t> arrayDimensions;

    // Return true for the special "no value" type used by void functions.
    //
    // Lowering frequently needs this check because MLIR function types represent "no result"
    // by omitting the result type entirely, not by using a first-class void value.
    bool isVoid() const {
        return arrayDimensions.empty() && kind == BuiltinTypeKind::Void;
    }

    bool isFixedArray() const { return !arrayDimensions.empty(); }

    Type afterIndex() const {
        Type t{.kind = kind, .arrayDimensions = arrayDimensions};
        if (!t.arrayDimensions.empty()) {
            t.arrayDimensions.erase(t.arrayDimensions.begin());
        }
        return t;
    }

    Type elementScalarType() const { return Type{.kind = kind, .arrayDimensions = {}}; }

    // Element scalar type for memref element MLIR lowering (leaf `T` of `[… x T]`).
    Type elementType() const;

    // Return true for fixed-width integer types.
    //
    // This excludes bool even though MLIR lowers bool to i1. At the nex language
    // level, bool is its own scalar type with condition semantics, not an integer
    // arithmetic type.
    bool isInteger() const;

    // Return true for IEEE-754 floating-point scalar types.
    bool isFloat() const;
};

// A ValueRef names a temporary value produced by an operation inside one const
// initializer or function body.
//
// Values print as `%0`, `%1`, and so on. They are not source variables; they are
// the intermediate results created while evaluating expressions.
struct ValueRef {
    std::size_t id = 0;
    Type type;
};

// A LocalRef names a function-local storage slot.
//
// Locals print as `$0`, `$1`, and so on. Parameters and `let` bindings both get
// local slots. Loading from a local creates a ValueRef; storing to a local
// changes the slot and does not itself produce a value.
struct LocalRef {
    std::size_t id = 0;
};

struct Block;

// Operation is the main instruction-like node in the typed IR.
//
// This is intentionally not SSA, MLIR, or LLVM IR yet. It is still a simple
// structured representation of checked nex programs. The union-like payload
// fields below are selected by `kind`; for example, Binary uses `op`, `left`,
// `right`, and `result`, while If uses `condition`, `thenBlock`, and
// optionally `elseBlock`. ShortCircuitAnd / ShortCircuitOr also use `left`,
// `thenBlock`, and `elseBlock`, but interpret the arms differently: see the
// `Operation::Kind` enumerators for the exact contract.
struct Operation {
    enum class Kind {
        Invalid,

        // Value-producing literals.
        IntegerLiteral,
        FloatLiteral,
        BoolLiteral,
        StringLiteral,

        // Resolved reads. `LoadLocal` reads a function local slot; `LoadConst`
        // reads a module-level constant by name.
        LoadLocal,
        LoadConst,

        // Value-producing expression operations.
        Unary,
        Binary,

        // Short-circuit logical AND (`&&`) and OR (`||`).
        //
        // These exist so backends can preserve the standard language rule: the
        // right-hand side is evaluated only when its value can change the result
        // (`&&` skips the RHS when the LHS is false; `||` skips it when the LHS is
        // true).
        //
        // Payload (both kinds share the same slot layout):
        //
        // - `left` (ValueRef): the **already evaluated** left-hand operand. Its type
        //   is any *condition-like* scalar: `bool` or a fixed-width integer. MLIR
        //   lowering first converts it to a truth test (`i1`) the same way `if` and
        //   `while` conditions are tested: `bool` is already `i1`; integers use
        //   `icmp ne %v, 0`.
        //
        // - `thenBlock` / `elseBlock` (structured child blocks): the two arms of
        //   an `scf.if`-shaped lowering over that truth test. Which arm evaluates
        //   the user’s RHS depends on the kind (see below).
        //
        // - `result` (bool ValueRef): the `bool` temporary produced by the operation.
        //
        // Child block terminators use `ReturnValue`, but that name is **scoped** to
        // the structured region, not to the surrounding function: lowering turns each
        // branch into `scf.yield` carrying an `i1`, *not* `func.return`. (The same
        // `ReturnValue` terminator is reused by `if` arms for the same reason.)
        //
        // ShortCircuitAnd (`&&`):
        //   If the truth test on `left` is true, `thenBlock` runs the user’s RHS,
        //   coerces its value to `bool` (integer `0` is false, any other integer is
        //   true; `bool` is identity), and ends with `ReturnValue` that boolean.
        //   If the test is false, `elseBlock` skips the RHS entirely and yields
        //   `false`.
        //
        // ShortCircuitOr (`||`):
        //   If the truth test on `left` is true, `thenBlock` skips the RHS and
        //   yields `true`. If false, `elseBlock` evaluates the RHS with the same
        //   truthify-to-bool contract as the `&&` “take RHS” arm above.
        //
        // Module-level `const` initializers do **not** use these operations. They
        // are analyzed as compile-time values and the IR builder lowers `&&` / `||`
        // there as plain `Binary` on **truthified** `bool` operands (see
        // `TypedIrBuilder` in `src/ir/builder.cpp`). That keeps const MLIR replay
        // linear and avoids duplicating region-based control flow in initializers.
        ShortCircuitAnd,
        ShortCircuitOr,

        Call,

        // Side-effecting local storage operations.
        //
        // `DeclareLocal` normally carries an initializing ValueRef. When the source
        // used `let mut x: T;` (no `=`), `value` is empty and backends allocate
        // stack storage without an initial store—reads are still rejected by
        // semantic definite-assignment checking.
        DeclareLocal,
        StoreLocal,

        // Fixed-size array value: element temporaries are listed in `arguments`.
        ArrayLiteral,

        // `base[index]` for array-typed SSA values (memref handles).
        IndexLoad,

        // `base[index] = value` for mutating an element.
        IndexStore,

        // Structured control flow. These remain structured on purpose so the
        // first IR dump is easy to understand and can later lower naturally to
        // MLIR `scf` or to explicit basic blocks.
        If,
        While,

        // Loop control (only valid inside `while` / `for` bodies; semantic analysis
        // enforces that). MLIR lowering uses a per-loop memref flag for `break` and
        // `scf.yield` for both forms.
        Break,
        Continue,
    };

    Kind kind = Kind::Invalid;

    // The source span that produced this operation. Keeping spans in IR lets
    // later backend/lowering diagnostics still point back to user code.
    SourceSpan span{};

    // Present only for operations that produce a value. Void calls, local
    // declarations, stores, and structured control-flow operations do not have a
    // result value.
    std::optional<ValueRef> result = std::nullopt;

    // Shared payload slots used by different operation kinds:
    //
    // - text: literal spelling, source name, or callable name
    // - boolValue: boolean literal payload
    // - isBuiltin: whether a call target should print as @builtin.name
    // - isMutable: whether a declared local came from `let mut`
    // - op: token-classified unary or binary operator
    std::string text{};
    bool boolValue = false;
    bool isBuiltin = false;
    bool isMutable = false;
    TokenKind op = TokenKind::EndOfFile;

    // Reference payloads for local access, expression operands, conditions, and
    // call arguments. Optional values are used where an operation kind may or may
    // not need that slot.
    LocalRef local{};
    std::optional<ValueRef> value = std::nullopt;
    std::optional<ValueRef> left = std::nullopt;
    std::optional<ValueRef> right = std::nullopt;
    std::optional<ValueRef> condition = std::nullopt;
    std::vector<ValueRef> arguments{};

    std::unique_ptr<Block> conditionBlock{};
    std::unique_ptr<Block> thenBlock{};
    std::unique_ptr<Block> elseBlock{};
    std::unique_ptr<Block> bodyBlock{};
    // Present only for `While` operations produced from `for` lowering. The main
    // body runs first; `continue` jumps to this block before yielding to the
    // next condition evaluation. Plain `while` leaves this unset.
    std::unique_ptr<Block> stepBlock{};
};

// A Terminator records the final meaning of a block when the block yields
// control-flow or initializer information.
//
// Some structured blocks currently have no terminator: for example, a while body
// that falls through to the next iteration. Later CFG lowering can make those
// edges explicit.
struct Terminator {
    enum class Kind {
        None,

        // Function returns.
        Return,
        ReturnValue,

        // Special structured-block results used by current IR forms.
        ConditionValue,
        InitValue,
    };

    Kind kind = Kind::None;
    SourceSpan span{};
    std::optional<ValueRef> value = std::nullopt;
};

// A Block is an ordered list of operations plus an optional terminator.
//
// The order matters: value IDs are assigned in emission order, and later
// lowering can walk operations from top to bottom.
struct Block {
    SourceSpan span{};
    std::vector<Operation> operations{};
    Terminator terminator{};
};

// Local describes one storage slot inside a function.
//
// Parameters are locals because function bodies read them the same way they read
// `let` bindings. The separate Kind value only preserves useful dump/debug
// information.
struct Local {
    enum class Kind {
        Parameter,
        Local,
    };

    LocalRef ref{};
    std::string name{};
    Type type{};
    bool isMutable = false;
    Kind kind = Kind::Local;
    SourceSpan span{};
};

// A Parameter connects the source-level function parameter to the local slot
// used by the body.
struct Parameter {
    std::string name{};
    Type type{};
    LocalRef local{};
    SourceSpan span{};
};

// Const represents a module-level `const`.
//
// The initializer is a block rather than a single expression because even simple
// expressions lower to a sequence of value-producing operations. `nextValueId`
// is local to this initializer block, just as a function has its own value ID
// namespace.
struct Const {
    std::string name{};
    Type type{};
    SourceSpan span{};
    Block initializer{};
    std::size_t nextValueId = 0;
};

// Function is the IR form of a nex function declaration.
//
// It stores parameters, all local slots discovered while building the body, the
// structured body block, and the next temporary value ID for that function.
struct Function {
    std::string name{};
    std::vector<Parameter> parameters{};
    Type returnType{};
    SourceSpan span{};
    std::vector<Local> locals{};
    Block body{};
    std::size_t nextValueId = 0;
};

// Module is the IR root for one source file in the current compiler.
//
// This mirrors nex's current top level: module constants plus functions. Future
// import/module work can make this root represent a larger compilation unit.
struct Module {
    std::vector<Const> constants{};
    std::vector<Function> functions{};
};

// Convert an IR type back to its source-facing spelling, such as `i32` or
// `bool`. The textual IR dump uses these names because the dump is for humans
// learning the compiler, not for machine parsing.
std::string_view typeName(Type type);

// Format a callable name for dumps.
//
// User functions print as `@name`. Compiler-provided built-ins print as
// `@builtin.name` so it is obvious they did not come from a source declaration.
std::string callableName(std::string_view name, bool isBuiltin);

// Format a temporary value name. ValueRefs are expression results, so the dump
// uses `%` just like MLIR/LLVM-style SSA values.
std::string valueName(ValueRef value);

// Format a local storage slot name. LocalRefs are assignable/readable places, so
// the dump uses `$` to keep them visually separate from `%` expression results.
std::string localName(LocalRef local);

} // namespace nexc::ir
