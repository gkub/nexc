#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexc {

// Decode a nex string literal token (including quotes) into UTF-8 bytes.
// Returns false on malformed input; optional `error` describes the failure.
bool decodeStringLiteralContent(std::string_view rawQuoted, std::string& outDecoded,
                                std::string* error = nullptr);

// Split a decoded format string into literal segments around `{}` placeholders.
// `literals.size()` is always `holeCount + 1`. On failure, returns false.
bool splitFormatString(std::string_view decoded, std::vector<std::string>& literalsOut,
                       std::string& error);

struct FormatHole {
    // Empty precision means the default formatting for the argument type. A
    // value means fixed fractional digits and is currently valid only for floats.
    std::optional<unsigned> precision;
};

struct FormatParts {
    std::vector<std::string> literals;
    std::vector<FormatHole> holes;
};

// Split a decoded format string into literal segments plus placeholder specs.
// Supported placeholders are `{}`, `{:.N}`, and `{:.Nf}`.
bool parseFormatString(std::string_view decoded, FormatParts& outParts,
                       std::string& error);

} // namespace nexc
