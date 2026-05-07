#include "nexc/frontend/ast.h"

#include <string>

namespace nexc {

namespace {

class AstDumper {
public:
    explicit AstDumper(std::ostream& out) : out_(out) {}

    void dump(const TranslationUnit& unit) {
        line("TranslationUnit");
        indent_ += 2;
        for (const std::unique_ptr<Item>& item : unit.items) {
            dumpItem(*item);
        }
        indent_ -= 2;
    }

private:
    void line(std::string_view text) {
        for (int i = 0; i < indent_; ++i) {
            out_ << ' ';
        }
        out_ << text << '\n';
    }

    void dumpItem(const Item& item) {
        if (const auto* function = dynamic_cast<const FunctionDecl*>(&item)) {
            line("FunctionDecl " + function->name);
            indent_ += 2;
            line("Parameters");
            indent_ += 2;
            for (const ParameterSyntax& parameter : function->parameters) {
                line("Parameter " + parameter.name + ": " +
                     std::string(builtinTypeName(parameter.type.kind)));
            }
            indent_ -= 2;
            line("ReturnType " +
                 std::string(builtinTypeName(function->returnType.kind)));
            dumpStmt(*function->body);
            indent_ -= 2;
            return;
        }

        if (const auto* constant = dynamic_cast<const ConstDecl*>(&item)) {
            line("ConstDecl " + constant->name + ": " +
                 std::string(builtinTypeName(constant->type.kind)));
            indent_ += 2;
            dumpExpr(*constant->init);
            indent_ -= 2;
        }
    }

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
                 let->name + ": " + std::string(builtinTypeName(let->type.kind)));
            indent_ += 2;
            dumpExpr(*let->init);
            indent_ -= 2;
            return;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            line("AssignStmt " + assign->name);
            indent_ += 2;
            dumpExpr(*assign->value);
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

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
            line("CallStmt");
            indent_ += 2;
            dumpExpr(*callStmt->call);
            indent_ -= 2;
        }
    }

    void dumpExpr(const Expr& expr) {
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
            line("IntegerLiteral " + integer->raw);
            return;
        }

        if (const auto* boolean = dynamic_cast<const BoolLiteralExpr*>(&expr)) {
            line(std::string("BoolLiteral ") + (boolean->value ? "true" : "false"));
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
        }
    }

    std::ostream& out_;
    int indent_ = 0;
};

} // namespace

std::string_view builtinTypeName(BuiltinTypeKind kind) {
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
    case BuiltinTypeKind::Void:
        return "void";
    case BuiltinTypeKind::Invalid:
        return "<invalid>";
    }

    return "<invalid>";
}

void dumpAst(std::ostream& out, const TranslationUnit& unit) {
    AstDumper(out).dump(unit);
}

} // namespace nexc
