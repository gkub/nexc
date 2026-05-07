#pragma once

#include "nexc/frontend/source.h"

#include <ostream>
#include <string_view>

namespace nexc {

// TokenKind is the parser's vocabulary. The lexer converts raw characters into
// these stable categories so the parser never has to reason about individual
// source bytes.
enum class TokenKind {
    EndOfFile,
    Identifier,
    IntegerLiteral,

    KwBool,
    KwConst,
    KwElse,
    KwFalse,
    KwFn,
    KwIf,
    KwLet,
    KwMut,
    KwReturn,
    KwTrue,
    KwVoid,
    KwWhile,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AmpAmp,
    PipePipe,
    Arrow,

    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Colon,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    SourceSpan span;
};

std::string_view tokenKindName(TokenKind kind);
TokenKind keywordKind(std::string_view text);

void dumpToken(std::ostream& out, const SourceFile& source, const Token& token);

} // namespace nexc
