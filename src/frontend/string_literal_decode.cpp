#include "nexc/frontend/string_literal_decode.h"

#include <charconv>
#include <system_error>
#include <utility>

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
    FormatParts parts;
    if (!parseFormatString(decoded, parts, error)) {
        literalsOut.clear();
        return false;
    }
    literalsOut = std::move(parts.literals);
    return true;
}

bool parseFormatString(std::string_view decoded, FormatParts& outParts,
                       std::string& error) {
    outParts = FormatParts{};
    std::size_t start = 0;
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i] != '{') {
            continue;
        }
        if (i + 1 < decoded.size() && decoded[i + 1] == '}') {
            outParts.literals.emplace_back(decoded.substr(start, i - start));
            outParts.holes.push_back(FormatHole{});
            start = i + 2;
            ++i;
            continue;
        }

        if (i + 3 < decoded.size() && decoded[i + 1] == ':' &&
            decoded[i + 2] == '.') {
            const std::size_t digitsBegin = i + 3;
            std::size_t cursor = digitsBegin;
            while (cursor < decoded.size() && decoded[cursor] >= '0' &&
                   decoded[cursor] <= '9') {
                ++cursor;
            }
            if (cursor == digitsBegin) {
                error = "invalid format string: expected precision digits after `{:.`";
                outParts = FormatParts{};
                return false;
            }
            if (cursor < decoded.size() && decoded[cursor] == 'f') {
                ++cursor;
            }
            if (cursor < decoded.size() && decoded[cursor] == '}') {
                unsigned precision = 0;
                const auto* begin = decoded.data() + digitsBegin;
                const auto* end = decoded.data() + cursor -
                                  (decoded[cursor - 1] == 'f' ? 1 : 0);
                const std::from_chars_result parsed =
                    std::from_chars(begin, end, precision, 10);
                if (parsed.ec != std::errc{} || parsed.ptr != end ||
                    precision > 18) {
                    error =
                        "invalid format string: float precision must be an integer from 0 to 18";
                    outParts = FormatParts{};
                    return false;
                }
                outParts.literals.emplace_back(decoded.substr(start, i - start));
                outParts.holes.push_back(FormatHole{.precision = precision});
                start = cursor + 1;
                i = cursor;
                continue;
            }
        }

        error = "invalid format string: expected `{}`, `{:.N}`, or `{:.Nf}` placeholder";
        outParts = FormatParts{};
        return false;
    }
    outParts.literals.emplace_back(decoded.substr(start));
    return true;
}

} // namespace nexc
