#include "elaboration_internal.h"

// Stable public runtime API: diagnostics, one-shot execution, and concrete
// source materialization. Stateful elaboration lives in the consumer files.
namespace fine {

    SemanticError::SemanticError(syntax::SourceSpan span, std::string message)
        : std::runtime_error(std::move(message)), span_(span) {}

    std::string SemanticError::format(std::string_view filename, std::string_view source) const {
        std::ostringstream output;
        output << filename << ':' << span_.begin.line << ':' << span_.begin.column << ": " << what();
        std::string line = elaboration::source_line(source, span_.begin.line);
        if (!line.empty()) {
            output << '\n' << line << '\n';
            std::size_t caret = span_.begin.column > 0 ? span_.begin.column - 1 : 0;
            output << std::string(caret, ' ') << '^';
        }
        return output.str();
    }

    ExecutionResult execute(syntax::Document const &document, std::ostream &output, std::ostream *rainfall_output,
                            SourceSnapshot const *snapshot, std::string rainfall_run, ExecutionOptions options) {
        return elaboration::DocumentRunner(output, rainfall_output, snapshot, std::move(rainfall_run), options)
            .execute(document);
    }

    std::string apply_materializations(syntax::ConcreteSyntaxTree const &tree,
                                       std::vector<Materialization> materializations) {
        std::string source = tree.render();
        std::sort(materializations.begin(), materializations.end(), [](auto const &left, auto const &right) {
            return std::pair{left.range.begin, left.range.end} < std::pair{right.range.begin, right.range.end};
        });
        std::ostringstream output;
        std::size_t cursor = 0;
        for (auto const &materialization : materializations) {
            if (materialization.range.begin < cursor || materialization.range.begin > materialization.range.end ||
                materialization.range.end > source.size())
                throw std::runtime_error("invalid or overlapping source materialization");
            output << source.substr(cursor, materialization.range.begin - cursor) << materialization.text;
            cursor = materialization.range.end;
        }
        output << source.substr(cursor);
        return output.str();
    }

}  // namespace fine
