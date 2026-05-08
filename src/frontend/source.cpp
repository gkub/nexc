#include "nexc/frontend/source.h"

#include <algorithm>

namespace nexc {

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
    // Store line starts instead of line/column on every token. Tokens and AST
    // nodes only need compact byte offsets while the compiler works; line/column
    // is derived lazily when rendering diagnostics.
    lineStarts_.push_back(0);

    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            // The next line starts immediately after the newline byte.
            lineStarts_.push_back(i + 1);
        }
    }
}

std::string_view SourceFile::slice(SourceSpan span) const {
    // Be forgiving about spans produced during error recovery. A bad span should
    // not crash diagnostic printing or token dumping.
    if (span.start > text_.size() || span.end < span.start) {
        return {};
    }

    const std::size_t clampedEnd = std::min(span.end, text_.size());
    return std::string_view(text_).substr(span.start, clampedEnd - span.start);
}

LineColumn SourceFile::lineColumn(std::size_t offset) const {
    const std::size_t clampedOffset = std::min(offset, text_.size());

    // upper_bound finds the first line start *after* the offset, so the previous
    // entry is the containing line. This makes lookup logarithmic in the number
    // of source lines instead of scanning from the beginning each time.
    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(),
                                     clampedOffset);
    const std::size_t lineIndex =
        it == lineStarts_.begin() ? 0 : static_cast<std::size_t>(it - lineStarts_.begin() - 1);

    return LineColumn{
        .line = lineIndex + 1,
        .column = clampedOffset - lineStarts_[lineIndex] + 1,
    };
}

} // namespace nexc
