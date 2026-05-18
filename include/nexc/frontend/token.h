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
    FloatLiteral,
    StringLiteral,

    KwBool,
    KwBreak,
    KwConst,
    KwContinue,
    KwElse,
    KwFalse,
    KwFor,
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
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Colon,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;

    // Token text is not stored here. The lexer records only the token kind and
    // span, and callers recover the spelling from SourceFile when needed. That
    // keeps tokens small and avoids copying every identifier/literal string.
    SourceSpan span;
};

// Convert token kinds to stable dump/debug names. These names are part of the
// token golden-test surface, so intentional changes should update goldens.
std::string_view tokenKindName(TokenKind kind);

// Return the keyword token for a spelling, or Identifier when the spelling is
// not reserved. This lets the lexer scan identifiers with one rule and classify
// keywords only after the full spelling is known.
TokenKind keywordKind(std::string_view text);

// Human-readable token dump used by `--dump-tokens` and golden tests.
void dumpToken(std::ostream& out, const SourceFile& source, const Token& token);

} // namespace nexc
