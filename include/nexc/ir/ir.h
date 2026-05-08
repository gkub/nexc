#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/frontend/source.h"
#include "nexc/frontend/token.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nexc::ir {

// IR Type is the backend-facing version of a Core v0 type.
//
// It deliberately wraps the frontend's BuiltinTypeKind for now. That keeps the
// first IR slice small while still making an important compiler idea explicit:
// once we are in IR, every produced value has a known type.
struct Type {
    BuiltinTypeKind kind = BuiltinTypeKind::Invalid;

    bool isVoid() const { return kind == BuiltinTypeKind::Void; }
    bool isInteger() const;
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
// structured representation of checked Core v0 programs. The union-like payload
// fields below are selected by `kind`; for example, Binary uses `op`, `left`,
// `right`, and `result`, while If uses `condition`, `thenBlock`, and
// optionally `elseBlock`.
struct Operation {
    enum class Kind {
        Invalid,

        // Value-producing literals.
        IntegerLiteral,
        BoolLiteral,
        StringLiteral,

        // Resolved reads. `LoadLocal` reads a function local slot; `LoadConst`
        // reads a module-level constant by name.
        LoadLocal,
        LoadConst,

        // Value-producing expression operations.
        Unary,
        Binary,
        Call,

        // Side-effecting local storage operations.
        DeclareLocal,
        StoreLocal,

        // Structured control flow. These remain structured on purpose so the
        // first IR dump is easy to understand and can later lower naturally to
        // MLIR `scf` or to explicit basic blocks.
        If,
        While,
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

// Function is the IR form of a Core v0 function declaration.
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
// This mirrors Core v0's top level: module constants plus functions. Future
// import/module work can make this root represent a larger compilation unit.
struct Module {
    std::vector<Const> constants{};
    std::vector<Function> functions{};
};

// Formatting helpers shared by the IR dumper and tests.
std::string_view typeName(Type type);
std::string callableName(std::string_view name, bool isBuiltin);
std::string valueName(ValueRef value);
std::string localName(LocalRef local);

} // namespace nexc::ir
