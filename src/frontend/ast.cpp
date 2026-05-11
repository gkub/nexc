#include "nexc/frontend/ast.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nexc {

namespace {

// AstDumper prints the AST as an indented tree.
//
// This view is intentionally source-shaped: it shows parser output before name
// resolution, type checking, or IR lowering. That makes it useful when learning
// whether the parser understood a piece of syntax the way we expected.
class AstDumper {
public:
    // Create a text dumper that writes to the caller's stream.
    //
    // The dumper owns only formatting state such as indentation; the AST itself
    // is borrowed and never modified.
    explicit AstDumper(std::ostream& out) : out_(out) {}

    // Dump the AST root.
    //
    // TranslationUnit is the file-level node, so every item in the source file
    // appears underneath it in source order.
    void dump(const TranslationUnit& unit) {
        line("TranslationUnit");
        indent_ += 2;
        for (const std::unique_ptr<Item>& item : unit.items) {
            dumpItem(*item);
        }
        indent_ -= 2;
    }

private:
    // Write one line with the current indentation level.
    //
    // All AST text output funnels through this helper so nested statements and
    // expressions stay visually aligned.
    void line(std::string_view text) {
        // The text dump uses spaces rather than tree-drawing characters so the
        // golden files stay plain ASCII and easy to update by hand.
        for (int i = 0; i < indent_; ++i) {
            out_ << ' ';
        }
        out_ << text << '\n';
    }

    // Dump one top-level declaration node.
    //
    // Items are polymorphic AST nodes, so the dumper recovers the concrete kind
    // and prints the fields that matter for that syntax form.
    void dumpItem(const Item& item) {
        // AST nodes are stored through base-class pointers, so dumping uses
        // dynamic_cast to recover the concrete node type. This is simple and
        // explicit for a small educational AST.
        if (const auto* function = dynamic_cast<const FunctionDecl*>(&item)) {
            line("FunctionDecl " + function->name);
            indent_ += 2;
            line("Parameters");
            indent_ += 2;
            for (const ParameterSyntax& parameter : function->parameters) {
                line("Parameter " + parameter.name + ": " + formatTypeSyntax(parameter.type));
            }
            indent_ -= 2;
            line("ReturnType " + formatTypeSyntax(function->returnType));
            dumpStmt(*function->body);
            indent_ -= 2;
            return;
        }

        if (const auto* constant = dynamic_cast<const ConstDecl*>(&item)) {
            line("ConstDecl " + constant->name + ": " + formatTypeSyntax(constant->type));
            indent_ += 2;
            dumpExpr(*constant->init);
            indent_ -= 2;
        }
    }

    // Dump one statement subtree.
    //
    // Statements are source-level control/effect forms: blocks, local bindings,
    // assignment, returns, control flow, and call statements.
    void dumpStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            line("BlockStmt");
            indent_ += 2;
            for (const std::unique_ptr<Stmt>& child : block->statements) {
                dumpStmt(*child);
            }
            indent_ -= 2;
            return;
        }

        if (const auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
            line(std::string(let->isMutable ? "LetStmt mut " : "LetStmt ") +
                 let->name + ": " + formatTypeSyntax(let->type));
            indent_ += 2;
            dumpExpr(*let->init);
            indent_ -= 2;
            return;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            line("AssignStmt");
            indent_ += 2;
            line("Target");
            indent_ += 2;
            dumpExpr(*assign->target);
            indent_ -= 2;
            line("Value");
            indent_ += 2;
            dumpExpr(*assign->value);
            indent_ -= 2;
            indent_ -= 2;
            return;
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            line("ReturnStmt");
            if (ret->value) {
                indent_ += 2;
                dumpExpr(*ret->value);
                indent_ -= 2;
            }
            return;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            line("IfStmt");
            indent_ += 2;
            line("Condition");
            indent_ += 2;
            dumpExpr(*ifStmt->condition);
            indent_ -= 2;
            line("Then");
            indent_ += 2;
            dumpStmt(*ifStmt->thenBranch);
            indent_ -= 2;
            if (ifStmt->elseBranch) {
                line("Else");
                indent_ += 2;
                dumpStmt(*ifStmt->elseBranch);
                indent_ -= 2;
            }
            indent_ -= 2;
            return;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            line("WhileStmt");
            indent_ += 2;
            line("Condition");
            indent_ += 2;
            dumpExpr(*whileStmt->condition);
            indent_ -= 2;
            line("Body");
            indent_ += 2;
            dumpStmt(*whileStmt->body);
            indent_ -= 2;
            indent_ -= 2;
            return;
        }

        if (const auto* forStmt = dynamic_cast<const ForStmt*>(&stmt)) {
            line("ForStmt");
            indent_ += 2;
            if (forStmt->init) {
                line("Init");
                indent_ += 2;
                dumpStmt(*forStmt->init);
                indent_ -= 2;
            }
            if (forStmt->condition) {
                line("Condition");
                indent_ += 2;
                dumpExpr(*forStmt->condition);
                indent_ -= 2;
            }
            if (forStmt->step) {
                line("Step");
                indent_ += 2;
                dumpStmt(*forStmt->step);
                indent_ -= 2;
            }
            line("Body");
            indent_ += 2;
            dumpStmt(*forStmt->body);
            indent_ -= 2;
            indent_ -= 2;
            return;
        }

        if (dynamic_cast<const BreakStmt*>(&stmt)) {
            line("BreakStmt");
            return;
        }

        if (dynamic_cast<const ContinueStmt*>(&stmt)) {
            line("ContinueStmt");
            return;
        }

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
            line("CallStmt");
            indent_ += 2;
            dumpExpr(*callStmt->call);
            indent_ -= 2;
        }
    }

    // Dump one expression subtree.
    //
    // Expressions produce values later, but at AST time they are still pure
    // syntax: names are unresolved, integer literal types are not final, and
    // parentheses are preserved.
    void dumpExpr(const Expr& expr) {
        // The dumper preserves source-level expression shape. For example,
        // ParenExpr is printed even though later semantic/IR stages can usually
        // ignore parentheses.
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
            line("IntegerLiteral " + integer->raw);
            return;
        }

        if (const auto* boolean = dynamic_cast<const BoolLiteralExpr*>(&expr)) {
            line(std::string("BoolLiteral ") + (boolean->value ? "true" : "false"));
            return;
        }

        if (const auto* string = dynamic_cast<const StringLiteralExpr*>(&expr)) {
            line("StringLiteral " + string->raw);
            return;
        }

        if (const auto* name = dynamic_cast<const NameExpr*>(&expr)) {
            line("NameExpr " + name->name);
            return;
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            line("CallExpr");
            indent_ += 2;
            line("Callee");
            indent_ += 2;
            dumpExpr(*call->callee);
            indent_ -= 2;
            line("Arguments");
            indent_ += 2;
            for (const std::unique_ptr<Expr>& argument : call->arguments) {
                dumpExpr(*argument);
            }
            indent_ -= 2;
            indent_ -= 2;
            return;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            line("UnaryExpr " + std::string(tokenKindName(unary->op)));
            indent_ += 2;
            dumpExpr(*unary->operand);
            indent_ -= 2;
            return;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            line("BinaryExpr " + std::string(tokenKindName(binary->op)));
            indent_ += 2;
            dumpExpr(*binary->left);
            dumpExpr(*binary->right);
            indent_ -= 2;
            return;
        }

        if (const auto* paren = dynamic_cast<const ParenExpr*>(&expr)) {
            line("ParenExpr");
            indent_ += 2;
            dumpExpr(*paren->inner);
            indent_ -= 2;
            return;
        }

        if (const auto* arrayLit = dynamic_cast<const ArrayLiteralExpr*>(&expr)) {
            line("ArrayLiteralExpr");
            indent_ += 2;
            for (const std::unique_ptr<Expr>& el : arrayLit->elements) {
                dumpExpr(*el);
            }
            indent_ -= 2;
            return;
        }

        if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(&expr)) {
            line("IndexExpr");
            indent_ += 2;
            line("Base");
            indent_ += 2;
            dumpExpr(*indexExpr->base);
            indent_ -= 2;
            line("Index");
            indent_ += 2;
            dumpExpr(*indexExpr->index);
            indent_ -= 2;
            indent_ -= 2;
            return;
        }
    }

    std::ostream& out_;
    int indent_ = 0;
};

// Escape text for a quoted Graphviz DOT label.
//
// DOT has its own string syntax. Without this escaping, a source string literal
// containing quotes or backslashes could produce invalid graph text.
std::string dotEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const char c : text) {
        // DOT labels use quoted strings, so characters that are meaningful to
        // DOT must be escaped before writing the graph.
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        default:
            escaped += c;
            break;
        }
    }

    return escaped;
}

// AstDotDumper emits the same AST shape as a Graphviz graph. The graph is useful
// when expression nesting or control-flow nesting is easier to understand
// visually than in the compact text dump.
class AstDotDumper {
public:
    // Create a Graphviz dumper that writes DOT text to the caller's stream.
    explicit AstDotDumper(std::ostream& out) : out_(out) {}

    // Emit one complete DOT graph for the AST.
    //
    // The graph starts with a synthetic TranslationUnit root, then connects each
    // AST node by role-labeled edges where useful.
    void dump(const TranslationUnit& unit) {
        out_ << "digraph NEX_AST {\n";
        out_ << "  graph [rankdir=TB];\n";
        out_ << "  node [shape=box, fontname=\"monospace\"];\n";
        out_ << "  edge [fontname=\"monospace\"];\n";

        const std::size_t root = node("TranslationUnit");
        for (const std::unique_ptr<Item>& item : unit.items) {
            edge(root, dumpItem(*item));
        }

        out_ << "}\n";
    }

private:
    // Emit a DOT node and return its synthetic numeric ID.
    //
    // Graphviz edges need stable node IDs, but users care about labels. The IDs
    // are deliberately implementation details local to one graph dump.
    std::size_t node(std::string_view label) {
        // Node IDs are synthetic and local to one graph. Labels carry the
        // educational content; IDs only let DOT connect edges.
        const std::size_t id = nextId_++;
        out_ << "  n" << id << " [label=\"" << dotEscape(label) << "\"];\n";
        return id;
    }

    // Emit a DOT edge between two previously-created nodes.
    //
    // Optional labels document child roles such as `left`, `right`, `condition`,
    // or `body`.
    void edge(std::size_t from, std::size_t to, std::string_view label = {}) {
        // Edge labels identify child roles such as `condition`, `then`, or
        // `body` where the relationship is not obvious from node order alone.
        out_ << "  n" << from << " -> n" << to;
        if (!label.empty()) {
            out_ << " [label=\"" << dotEscape(label) << "\"]";
        }
        out_ << ";\n";
    }

    // Dump one item and return the DOT node ID that represents its root.
    //
    // Returning the ID lets the caller connect this subtree to its parent.
    std::size_t dumpItem(const Item& item) {
        if (const auto* function = dynamic_cast<const FunctionDecl*>(&item)) {
            const std::size_t id = node("FunctionDecl\n" + function->name);
            const std::size_t params = node("Parameters");
            edge(id, params);
            for (const ParameterSyntax& parameter : function->parameters) {
                edge(params, node("Parameter\n" + parameter.name + ": " +
                                  formatTypeSyntax(parameter.type)));
            }
            edge(id, node("ReturnType\n" + formatTypeSyntax(function->returnType)));
            edge(id, dumpStmt(*function->body), "body");
            return id;
        }

        if (const auto* constant = dynamic_cast<const ConstDecl*>(&item)) {
            const std::size_t id =
                node("ConstDecl\n" + constant->name + ": " + formatTypeSyntax(constant->type));
            edge(id, dumpExpr(*constant->init), "init");
            return id;
        }

        return node("<unknown item>");
    }

    // Dump one statement subtree and return its root DOT node ID.
    std::size_t dumpStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            const std::size_t id = node("BlockStmt");
            for (const std::unique_ptr<Stmt>& child : block->statements) {
                edge(id, dumpStmt(*child));
            }
            return id;
        }

        if (const auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
            const std::size_t id =
                node(std::string(let->isMutable ? "LetStmt mut\n" : "LetStmt\n") +
                     let->name + ": " + formatTypeSyntax(let->type));
            edge(id, dumpExpr(*let->init), "init");
            return id;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            const std::size_t id = node("AssignStmt");
            edge(id, dumpExpr(*assign->target), "target");
            edge(id, dumpExpr(*assign->value), "value");
            return id;
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            const std::size_t id = node("ReturnStmt");
            if (ret->value) {
                edge(id, dumpExpr(*ret->value), "value");
            }
            return id;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            const std::size_t id = node("IfStmt");
            edge(id, dumpExpr(*ifStmt->condition), "condition");
            edge(id, dumpStmt(*ifStmt->thenBranch), "then");
            if (ifStmt->elseBranch) {
                edge(id, dumpStmt(*ifStmt->elseBranch), "else");
            }
            return id;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            const std::size_t id = node("WhileStmt");
            edge(id, dumpExpr(*whileStmt->condition), "condition");
            edge(id, dumpStmt(*whileStmt->body), "body");
            return id;
        }

        if (const auto* forStmt = dynamic_cast<const ForStmt*>(&stmt)) {
            const std::size_t id = node("ForStmt");
            if (forStmt->init) {
                edge(id, dumpStmt(*forStmt->init), "init");
            }
            if (forStmt->condition) {
                edge(id, dumpExpr(*forStmt->condition), "condition");
            }
            if (forStmt->step) {
                edge(id, dumpStmt(*forStmt->step), "step");
            }
            edge(id, dumpStmt(*forStmt->body), "body");
            return id;
        }

        if (dynamic_cast<const BreakStmt*>(&stmt)) {
            return node("BreakStmt");
        }

        if (dynamic_cast<const ContinueStmt*>(&stmt)) {
            return node("ContinueStmt");
        }

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
            const std::size_t id = node("CallStmt");
            edge(id, dumpExpr(*callStmt->call));
            return id;
        }

        return node("<unknown stmt>");
    }

    // Dump one expression subtree and return its root DOT node ID.
    std::size_t dumpExpr(const Expr& expr) {
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
            return node("IntegerLiteral\n" + integer->raw);
        }

        if (const auto* boolean = dynamic_cast<const BoolLiteralExpr*>(&expr)) {
            return node(std::string("BoolLiteral\n") +
                        (boolean->value ? "true" : "false"));
        }

        if (const auto* string = dynamic_cast<const StringLiteralExpr*>(&expr)) {
            return node("StringLiteral\n" + string->raw);
        }

        if (const auto* name = dynamic_cast<const NameExpr*>(&expr)) {
            return node("NameExpr\n" + name->name);
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            const std::size_t id = node("CallExpr");
            edge(id, dumpExpr(*call->callee), "callee");
            for (const std::unique_ptr<Expr>& argument : call->arguments) {
                edge(id, dumpExpr(*argument), "arg");
            }
            return id;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            const std::size_t id =
                node("UnaryExpr\n" + std::string(tokenKindName(unary->op)));
            edge(id, dumpExpr(*unary->operand), "operand");
            return id;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            const std::size_t id =
                node("BinaryExpr\n" + std::string(tokenKindName(binary->op)));
            edge(id, dumpExpr(*binary->left), "left");
            edge(id, dumpExpr(*binary->right), "right");
            return id;
        }

        if (const auto* paren = dynamic_cast<const ParenExpr*>(&expr)) {
            const std::size_t id = node("ParenExpr");
            edge(id, dumpExpr(*paren->inner), "inner");
            return id;
        }

        if (const auto* arrayLit = dynamic_cast<const ArrayLiteralExpr*>(&expr)) {
            const std::size_t id = node("ArrayLiteralExpr");
            for (const std::unique_ptr<Expr>& el : arrayLit->elements) {
                edge(id, dumpExpr(*el), "elem");
            }
            return id;
        }

        if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(&expr)) {
            const std::size_t id = node("IndexExpr");
            edge(id, dumpExpr(*indexExpr->base), "base");
            edge(id, dumpExpr(*indexExpr->index), "index");
            return id;
        }

        return node("<unknown expr>");
    }

    std::ostream& out_;
    std::size_t nextId_ = 0;
};

} // namespace

// Return the canonical source spelling for a built-in type kind.
//
// This helper is shared by AST dumps, diagnostics, and IR dumps so the project
// does not accidentally print the same type in multiple inconsistent ways.
std::string formatTypeSyntax(TypeSyntax syntax) {
    if (syntax.form == TypeSyntaxKind::Builtin) {
        return std::string(builtinTypeName(syntax.kind));
    }
    return "[" + std::string(builtinTypeName(syntax.arrayElementKind)) + "; " +
           std::to_string(syntax.arrayLength) + "]";
}

std::string_view builtinTypeName(BuiltinTypeKind kind) {
    // This spelling is shared by AST dumps, diagnostics, and IR dumps. Keeping
    // it centralized prevents drift between compiler stages.
    switch (kind) {
    case BuiltinTypeKind::I8:
        return "i8";
    case BuiltinTypeKind::I16:
        return "i16";
    case BuiltinTypeKind::I32:
        return "i32";
    case BuiltinTypeKind::I64:
        return "i64";
    case BuiltinTypeKind::U8:
        return "u8";
    case BuiltinTypeKind::U16:
        return "u16";
    case BuiltinTypeKind::U32:
        return "u32";
    case BuiltinTypeKind::U64:
        return "u64";
    case BuiltinTypeKind::Bool:
        return "bool";
    case BuiltinTypeKind::Str:
        return "str";
    case BuiltinTypeKind::Void:
        return "void";
    case BuiltinTypeKind::Invalid:
        return "<invalid>";
    }

    return "<invalid>";
}

// Public entry point for the compact textual AST dump.
void dumpAst(std::ostream& out, const TranslationUnit& unit) {
    AstDumper(out).dump(unit);
}

// Public entry point for Graphviz DOT AST output.
//
// The resulting DOT can be rendered with Graphviz into SVG/PNG for visual
// inspection of parser output.
void dumpAstDot(std::ostream& out, const TranslationUnit& unit) {
    AstDotDumper(out).dump(unit);
}

} // namespace nexc
