#include "nexc/ir/ir.h"

#include <string>

namespace nexc::ir {

Type Type::elementType() const {
    return Type{.shape = Shape::Scalar, .kind = elementKind};
}

// Keep integer classification close to the IR type wrapper. Later, if Type
// grows beyond BuiltinTypeKind, lowering code can continue asking this semantic
// question without knowing the exact representation.
bool Type::isInteger() const {
    if (shape != Shape::Scalar) {
        return false;
    }
    switch (kind) {
    case BuiltinTypeKind::I8:
    case BuiltinTypeKind::I16:
    case BuiltinTypeKind::I32:
    case BuiltinTypeKind::I64:
    case BuiltinTypeKind::U8:
    case BuiltinTypeKind::U16:
    case BuiltinTypeKind::U32:
    case BuiltinTypeKind::U64:
        return true;
    case BuiltinTypeKind::Bool:
    case BuiltinTypeKind::Str:
    case BuiltinTypeKind::Void:
    case BuiltinTypeKind::Invalid:
        return false;
    }

    return false;
}

// The IR currently reuses the frontend spelling for built-in type names. That
// keeps AST dumps, semantic diagnostics, and IR dumps visually consistent.
std::string_view typeName(Type type) {
    static thread_local std::string scratch;
    if (type.shape == Type::Shape::FixedArray) {
        scratch = "[";
        scratch += builtinTypeName(type.elementKind);
        scratch += "; ";
        scratch += std::to_string(type.arrayLength);
        scratch += "]";
        return scratch;
    }
    return builtinTypeName(type.kind);
}

// User functions/constants print as @name. Built-ins print as @builtin.name so
// the dump makes it clear they were supplied by the compiler, not by source.
std::string callableName(std::string_view name, bool isBuiltin) {
    return std::string(isBuiltin ? "@builtin." : "@") + std::string(name);
}

// Value and local prefixes intentionally differ:
//
// - %n is a temporary expression result
// - $n is a function storage slot
//
// Seeing both in dumps teaches the difference between "computed value" and
// "place that can be loaded from or stored to."
std::string valueName(ValueRef value) {
    return "%" + std::to_string(value.id);
}

// Format a local storage slot name for IR dumps.
//
// Locals are "places" rather than expression results, so they use `$` instead
// of `%`. That visual distinction matters once `let mut` and assignment appear.
std::string localName(LocalRef local) {
    return "$" + std::to_string(local.id);
}

} // namespace nexc::ir
