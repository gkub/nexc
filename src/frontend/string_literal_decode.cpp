#include "nexc/frontend/string_literal_decode.h"

namespace nexc {

bool decodeStringLiteralContent(std::string_view rawQuoted, std::string& outDecoded,
                                std::string* error) {
    outDecoded.clear();
    if (rawQuoted.size() < 2 || rawQuoted.front() != '"' || rawQuoted.back() != '"') {
        if (error) {
            *error = "string literal must be enclosed in double quotes";
        }
        return false;
    }

    for (std::size_t i = 1; i + 1 < rawQuoted.size(); ++i) {
        const char c = rawQuoted[i];
        if (c != '\\') {
            outDecoded.push_back(c);
            continue;
        }

        if (i + 2 >= rawQuoted.size()) {
            if (error) {
                *error = "string literal ended inside an escape sequence";
            }
            return false;
        }
        const char escaped = rawQuoted[++i];
        switch (escaped) {
        case '"':
            outDecoded.push_back('"');
            break;
        case '\\':
            outDecoded.push_back('\\');
            break;
        case 'n':
            outDecoded.push_back('\n');
            break;
        case 't':
            outDecoded.push_back('\t');
            break;
        case 'r':
            outDecoded.push_back('\r');
            break;
        default:
            if (error) {
                *error = "unsupported string escape sequence";
            }
            return false;
        }
    }
    return true;
}

bool splitFormatString(std::string_view decoded, std::vector<std::string>& literalsOut,
                       std::string& error) {
    literalsOut.clear();
    std::size_t start = 0;
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i] != '{') {
            continue;
        }
        if (i + 1 < decoded.size() && decoded[i + 1] == '}') {
            literalsOut.emplace_back(decoded.substr(start, i - start));
            start = i + 2;
            ++i;
            continue;
        }
        error = "invalid format string: expected `{}` placeholder or escape `{{` is "
                "not implemented yet";
        literalsOut.clear();
        return false;
    }
    literalsOut.emplace_back(decoded.substr(start));
    return true;
}

} // namespace nexc
