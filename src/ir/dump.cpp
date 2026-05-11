#include "nexc/ir/dump.h"

#include <ostream>
#include <stdexcept>
#include <string>

namespace nexc::ir {

namespace {

// ModuleDumper owns the stable text format used by `--dump-ir` and golden
// tests. Keeping the formatting logic in one class makes it easy to preserve
// indentation and naming conventions as new IR operations are added.
class ModuleDumper {
public:
    // Remember the output stream that receives the textual dump. The dumper does
    // not own the stream; it just writes one stable view of an already-built IR
    // module into it.
    explicit ModuleDumper(std::ostream& out) : out_(out) {}

    // Print the module root, then each top-level IR item underneath it.
    //
    // This is intentionally deterministic: items are printed in source order so
    // golden tests change only when the IR shape actually changes.
    void dump(const Module& module) {
        line("Module");
        indent_ += 2;

        for (const Const& constant : module.constants) {
            dumpConst(constant);
        }

        for (const Function& function : module.functions) {
            dumpFunction(function);
        }

        indent_ -= 2;
    }

private:
    // Print a module-level const and the block that computes its initializer.
    //
    // Consts do not yet lower to MLIR, but dumping them now keeps the typed IR
    // honest and makes future constant lowering easier to test.
    void dumpConst(const Const& constant) {
        // Const initializers are printed as blocks because they are lowered as a
        // sequence of operations ending in InitValue, not as raw AST expressions.
        line("Const " + callableName(constant.name, false) + ": " +
             std::string(typeName(constant.type)));
        indent_ += 2;
        dumpBlock(constant.initializer);
        indent_ -= 2;
    }

    // Print a function header, local table, and body block.
    //
    // The header includes parameter-local mappings (`$0 a: i32`) because later
    // LoadLocal operations refer to `$0`, not the original source spelling `a`.
    void dumpFunction(const Function& function) {
        // The function header shows source parameter names and their local slot
        // numbers. That makes the later LoadLocal operations easier to read.
        std::string header = "Function " + callableName(function.name, false) + "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i != 0) {
                header += ", ";
            }
            const Parameter& parameter = function.parameters[i];
            header += localName(parameter.local) + " " + parameter.name + ": " +
                      std::string(typeName(parameter.type));
        }
        header += ") -> ";
        header += std::string(typeName(function.returnType));
        line(header);

        indent_ += 2;
        dumpLocals(function);
        dumpBlock(function.body);
        indent_ -= 2;
    }

    // Print every local storage slot known to a function.
    //
    // This includes parameters and `let` bindings. Keeping the table separate
    // from the operation stream makes it easier to read loads/stores without
    // hunting backward through the dump for the declaration site.
    void dumpLocals(const Function& function) {
        if (function.locals.empty()) {
            return;
        }

        line("Locals");
        indent_ += 2;
        for (const Local& local : function.locals) {
            // Locals are listed before the body so the reader can map `$n`
            // references back to source names while reading the operation dump.
            std::string text = localName(local.ref) + " " + local.name + ": " +
                               std::string(typeName(local.type));
            if (local.kind == Local::Kind::Parameter) {
                text += " parameter";
            } else if (local.isMutable) {
                text += " mutable";
            }
            line(text);
        }
        indent_ -= 2;
    }

    // Print one IR block: first its ordered operations, then its terminator.
    //
    // A block's operation order matters because later operations can refer to
    // earlier ValueRefs. The textual format preserves that order exactly.
    void dumpBlock(const Block& block) {
        line("Block");
        indent_ += 2;
        for (const Operation& operation : block.operations) {
            dumpOperation(operation);
        }
        dumpTerminator(block.terminator);
        indent_ -= 2;
    }

    // Print one operation according to the payload selected by Operation::Kind.
    //
    // Operation is intentionally a compact union-like structure. This switch is
    // the readable map from each kind to the fields that matter for that kind.
    void dumpOperation(const Operation& operation) {
        // Operation payloads are union-like: each Kind selects which fields are
        // meaningful. The switch keeps that mapping explicit for readers.
        switch (operation.kind) {
        case Operation::Kind::Invalid:
            throw std::logic_error("typed IR dump encountered an invalid operation");
        case Operation::Kind::IntegerLiteral:
            dumpResultPrefix(operation);
            out_ << "IntegerLiteral " << operation.text << '\n';
            return;
        case Operation::Kind::BoolLiteral:
            dumpResultPrefix(operation);
            out_ << "BoolLiteral " << (operation.boolValue ? "true" : "false")
                 << '\n';
            return;
        case Operation::Kind::StringLiteral:
            dumpResultPrefix(operation);
            out_ << "StringLiteral " << operation.text << '\n';
            return;
        case Operation::Kind::LoadLocal:
            dumpResultPrefix(operation);
            out_ << "LoadLocal " << localName(operation.local) << '\n';
            return;
        case Operation::Kind::LoadConst:
            dumpResultPrefix(operation);
            out_ << "LoadConst " << callableName(operation.text, false) << '\n';
            return;
        case Operation::Kind::Unary:
            dumpResultPrefix(operation);
            out_ << "Unary " << tokenKindName(operation.op) << ' '
                 << valueName(requiredValue(operation.value)) << '\n';
            return;
        case Operation::Kind::Binary:
            dumpResultPrefix(operation);
            out_ << "Binary " << tokenKindName(operation.op) << ' '
                 << valueName(requiredValue(operation.left)) << ", "
                 << valueName(requiredValue(operation.right)) << '\n';
            return;
        case Operation::Kind::Call:
            dumpCall(operation);
            return;
        case Operation::Kind::DeclareLocal:
            line("DeclareLocal " + localName(operation.local) +
                 (operation.isMutable ? " mutable = " : " = ") +
                 valueName(requiredValue(operation.value)));
            return;
        case Operation::Kind::StoreLocal:
            line("StoreLocal " + localName(operation.local) + " = " +
                 valueName(requiredValue(operation.value)));
            return;
        case Operation::Kind::ArrayLiteral: {
            dumpResultPrefix(operation);
            out_ << "ArrayLiteral";
            for (std::size_t i = 0; i < operation.arguments.size(); ++i) {
                if (i != 0) {
                    out_ << ", ";
                }
                out_ << valueName(operation.arguments[i]);
            }
            out_ << '\n';
            return;
        }
        case Operation::Kind::IndexLoad:
            dumpResultPrefix(operation);
            out_ << "IndexLoad " << valueName(requiredValue(operation.left)) << "["
                 << valueName(requiredValue(operation.right)) << "]\n";
            return;
        case Operation::Kind::IndexStore:
            line("IndexStore " + valueName(requiredValue(operation.left)) + "[" +
                 valueName(requiredValue(operation.right)) + "] = " +
                 valueName(requiredValue(operation.value)));
            return;
        case Operation::Kind::If:
            dumpIf(operation);
            return;
        case Operation::Kind::While:
            dumpWhile(operation);
            return;
        case Operation::Kind::Break:
            line("Break");
            return;
        case Operation::Kind::Continue:
            line("Continue");
            return;
        }
    }

    // Print a call operation, with or without a result prefix.
    //
    // `println("x");` is a void call statement, so it prints as a bare Call.
    // `let x: i32 = f();` is a value-producing call, so it prints with `%n: T =`.
    void dumpCall(const Operation& operation) {
        // Void calls have no result prefix. Non-void calls look like other
        // value-producing operations: `%n: type = Call @f(...)`.
        if (operation.result) {
            dumpResultPrefix(operation);
        } else {
            writeIndent();
        }

        out_ << "Call " << callableName(operation.text, operation.isBuiltin) << "(";
        for (std::size_t i = 0; i < operation.arguments.size(); ++i) {
            if (i != 0) {
                out_ << ", ";
            }
            out_ << valueName(operation.arguments[i]);
        }
        out_ << ")\n";
    }

    // Print a structured if operation with nested Then/Else blocks.
    //
    // This is deliberately not a control-flow graph dump. The typed IR still
    // remembers source structure so the output teaches what the frontend built
    // before MLIR or LLVM flattening decisions happen.
    void dumpIf(const Operation& operation) {
        // Structured control flow is printed with nested blocks instead of
        // labels/branches because this first IR has not lowered to a CFG yet.
        line("If " + valueName(requiredValue(operation.condition)));
        indent_ += 2;
        line("Then");
        indent_ += 2;
        dumpBlock(requiredBlock(operation.thenBlock));
        indent_ -= 2;
        if (operation.elseBlock) {
            line("Else");
            indent_ += 2;
            dumpBlock(*operation.elseBlock);
            indent_ -= 2;
        }
        indent_ -= 2;
    }

    // Print a structured while operation.
    //
    // The condition is its own block because condition expressions can have
    // multiple operations before the final condition value is known.
    void dumpWhile(const Operation& operation) {
        // A while condition is itself a block. This handles conditions such as
        // `x + 1 < y`, where multiple operations are needed before the final
        // bool condition value exists.
        line("While");
        indent_ += 2;
        line("Condition");
        indent_ += 2;
        dumpBlock(requiredBlock(operation.conditionBlock));
        indent_ -= 2;
        line("Body");
        indent_ += 2;
        dumpBlock(requiredBlock(operation.bodyBlock));
        indent_ -= 2;
        if (operation.stepBlock) {
            line("Step");
            indent_ += 2;
            dumpBlock(*operation.stepBlock);
            indent_ -= 2;
        }
        indent_ -= 2;
    }

    // Print the optional block terminator.
    //
    // Terminators describe how a block completes: return from a function, yield a
    // condition value, yield a const initializer value, or fall through.
    void dumpTerminator(const Terminator& terminator) {
        // Terminators are optional at this IR level. If a block has no explicit
        // terminator, it falls through according to the enclosing structured
        // operation.
        switch (terminator.kind) {
        case Terminator::Kind::None:
            return;
        case Terminator::Kind::Return:
            line("Return");
            return;
        case Terminator::Kind::ReturnValue:
            line("ReturnValue " + valueName(requiredValue(terminator.value)));
            return;
        case Terminator::Kind::ConditionValue:
            line("ConditionValue " + valueName(requiredValue(terminator.value)));
            return;
        case Terminator::Kind::InitValue:
            line("InitValue " + valueName(requiredValue(terminator.value)));
            return;
        }
    }

    // Print the shared `%id: type =` prefix for value-producing operations.
    //
    // Keeping this in one helper prevents tiny formatting differences from
    // spreading across many operation cases and destabilizing golden tests.
    void dumpResultPrefix(const Operation& operation) {
        // Most value-producing operations share the same printed prefix:
        // `%id: type =`. Centralizing it keeps golden files consistent.
        const ValueRef result = requiredValue(operation.result);
        writeIndent();
        out_ << valueName(result) << ": " << typeName(result.type) << " = ";
    }

    // Unwrap a required ValueRef payload or fail loudly if the IR is malformed.
    //
    // These are internal sanity checks. A user source error should be caught by
    // semantic analysis long before the dumper sees the IR.
    static ValueRef requiredValue(const std::optional<ValueRef>& value) {
        if (!value) {
            // These checks catch mismatches between Operation::Kind and payload
            // fields while developing the compiler. User programs should never
            // trigger them after semantic analysis has succeeded.
            throw std::logic_error("typed IR dump expected a value");
        }
        return *value;
    }

    // Unwrap a required child block from a structured operation.
    //
    // If this fails, the builder created an If/While operation without the block
    // shape that the operation kind promises.
    static const Block& requiredBlock(const std::unique_ptr<Block>& block) {
        if (!block) {
            // Structured operations always own their required child blocks. A
            // missing block means the builder and dumper disagree about IR shape.
            throw std::logic_error("typed IR dump expected a block");
        }
        return *block;
    }

    // Write one already-formatted logical line at the current indentation level.
    void line(const std::string& text) {
        writeIndent();
        out_ << text << '\n';
    }

    // Emit spaces for the current indentation level.
    //
    // The dumper tracks indentation as a simple count of spaces because the dump
    // format is intentionally small and stable for golden tests.
    void writeIndent() {
        for (int i = 0; i < indent_; ++i) {
            out_ << ' ';
        }
    }

    std::ostream& out_;
    int indent_ = 0;
};

} // namespace

// Public entry point for `--dump-ir`.
//
// All formatting state stays inside ModuleDumper so callers only need to pass an
// output stream and a completed typed IR module.
void dumpModule(std::ostream& out, const Module& module) {
    ModuleDumper(out).dump(module);
}

} // namespace nexc::ir
