#include "nexc/frontend/token.h"

#include <iomanip>

namespace nexc {

std::string_view tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile:
        return "EndOfFile";
    case TokenKind::Identifier:
        return "Identifier";
    case TokenKind::IntegerLiteral:
        return "IntegerLiteral";
    case TokenKind::StringLiteral:
        return "StringLiteral";
    case TokenKind::KwBool:
        return "KwBool";
    case TokenKind::KwConst:
        return "KwConst";
    case TokenKind::KwElse:
        return "KwElse";
    case TokenKind::KwFalse:
        return "KwFalse";
    case TokenKind::KwFn:
        return "KwFn";
    case TokenKind::KwIf:
        return "KwIf";
    case TokenKind::KwLet:
        return "KwLet";
    case TokenKind::KwMut:
        return "KwMut";
    case TokenKind::KwReturn:
        return "KwReturn";
    case TokenKind::KwTrue:
        return "KwTrue";
    case TokenKind::KwVoid:
        return "KwVoid";
    case TokenKind::KwWhile:
        return "KwWhile";
    case TokenKind::Plus:
        return "Plus";
    case TokenKind::Minus:
        return "Minus";
    case TokenKind::Star:
        return "Star";
    case TokenKind::Slash:
        return "Slash";
    case TokenKind::Percent:
        return "Percent";
    case TokenKind::Bang:
        return "Bang";
    case TokenKind::Equal:
        return "Equal";
    case TokenKind::EqualEqual:
        return "EqualEqual";
    case TokenKind::BangEqual:
        return "BangEqual";
    case TokenKind::Less:
        return "Less";
    case TokenKind::LessEqual:
        return "LessEqual";
    case TokenKind::Greater:
        return "Greater";
    case TokenKind::GreaterEqual:
        return "GreaterEqual";
    case TokenKind::AmpAmp:
        return "AmpAmp";
    case TokenKind::PipePipe:
        return "PipePipe";
    case TokenKind::Arrow:
        return "Arrow";
    case TokenKind::LeftParen:
        return "LeftParen";
    case TokenKind::RightParen:
        return "RightParen";
    case TokenKind::LeftBrace:
        return "LeftBrace";
    case TokenKind::RightBrace:
        return "RightBrace";
    case TokenKind::Comma:
        return "Comma";
    case TokenKind::Semicolon:
        return "Semicolon";
    case TokenKind::Colon:
        return "Colon";
    }

    return "Unknown";
}

TokenKind keywordKind(std::string_view text) {
    if (text == "bool") {
        return TokenKind::KwBool;
    }
    if (text == "const") {
        return TokenKind::KwConst;
    }
    if (text == "else") {
        return TokenKind::KwElse;
    }
    if (text == "false") {
        return TokenKind::KwFalse;
    }
    if (text == "fn") {
        return TokenKind::KwFn;
    }
    if (text == "if") {
        return TokenKind::KwIf;
    }
    if (text == "let") {
        return TokenKind::KwLet;
    }
    if (text == "mut") {
        return TokenKind::KwMut;
    }
    if (text == "return") {
        return TokenKind::KwReturn;
    }
    if (text == "true") {
        return TokenKind::KwTrue;
    }
    if (text == "void") {
        return TokenKind::KwVoid;
    }
    if (text == "while") {
        return TokenKind::KwWhile;
    }

    return TokenKind::Identifier;
}

void dumpToken(std::ostream& out, const SourceFile& source, const Token& token) {
    out << tokenKindName(token.kind) << " [" << token.span.start << ", "
        << token.span.end << ')';

    if (token.kind == TokenKind::Identifier ||
        token.kind == TokenKind::IntegerLiteral ||
        token.kind == TokenKind::StringLiteral) {
        out << " `" << source.slice(token.span) << '`';
    }

    out << '\n';
}

} // namespace nexc
