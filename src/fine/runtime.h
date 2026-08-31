#pragma once

#include "parser.h"

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fine {

class SemanticError : public std::runtime_error {
public:
    SemanticError(syntax::SourceSpan span, std::string message);

    syntax::SourceSpan span() const noexcept { return span_; }
    std::string format(std::string_view filename, std::string_view source) const;

private:
    syntax::SourceSpan span_;
};

// Elaborate and execute the one admitted proof in a parsed Fine document.
// Returns zero for a returned model and throws SemanticError for invalid Fine.
// When rainfall_output is non-null, Fine also writes a JSONL semantic trace.
int execute(syntax::Document const& document, std::ostream& output,
            std::ostream* rainfall_output = nullptr);

} // namespace fine
