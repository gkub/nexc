#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nexc {

// A half-open byte range into a SourceFile: [start, end).
//
// Compilers usually attach spans to both tokens and AST nodes so every error can
// point back to the source text that caused it. nex stores byte offsets first
// and derives line/column only when rendering diagnostics.
struct SourceSpan {
    std::size_t start = 0;
    std::size_t end = 0;
};

struct LineColumn {
    std::size_t line = 1;
    std::size_t column = 1;
};

// SourceFile is the compiler's owned view of one input file.
//
// Almost every frontend data structure stores SourceSpan byte offsets instead
// of copying source text. SourceFile is the object that can later turn those
// offsets back into slices, line numbers, columns, and caret diagnostics.
class SourceFile {
public:
    SourceFile(std::string path, std::string text);

    std::string_view path() const { return path_; }
    std::string_view text() const { return text_; }
    std::size_t size() const { return text_.size(); }

    // Return the source text covered by a span. The end is clamped so callers
    // can safely ask for text even after an earlier recovery placeholder used a
    // span near EOF.
    std::string_view slice(SourceSpan span) const;

    // Convert a byte offset into user-facing 1-based line/column coordinates.
    // The compiler stores byte offsets internally because they are compact and
    // easy to carry through tokens and AST nodes.
    LineColumn lineColumn(std::size_t offset) const;

private:
    std::string path_;
    std::string text_;

    // Start offset for each source line. This tiny index makes it cheap to turn
    // a byte offset into human-readable line/column coordinates for diagnostics.
    std::vector<std::size_t> lineStarts_;
};

} // namespace nexc
