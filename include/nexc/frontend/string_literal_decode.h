#pragma once

#include <cstddef>
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

} // namespace nexc
