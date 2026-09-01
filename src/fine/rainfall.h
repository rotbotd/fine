#pragma once

#include "parser.h"
#include "source.h"
#include "c++/z3++.h"

#include <cstddef>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fine {

    // One JSON field whose value has already been encoded. The recorder owns the
    // envelope and the term registry; producers only supply operation-specific
    // data. This deliberately keeps chronology separate from evidence references.
    struct RainfallField {
        std::string name;
        std::string json;
    };

    class RainfallRecorder {
    public:
        RainfallRecorder(z3::context &context, std::ostream &output, std::string run = {},
                         SourceSnapshot const *snapshot = nullptr);

        // Registers a strong z3::expr reference and returns its recorder-scoped
        // reference. Handles are never reused. Z3 AST IDs are emitted only as
        // diagnostics and are not used to establish identity.
        std::string term(z3::expr const &expression, std::string_view representation = "semantic-z3");

        // Reparse and exact-reify every term registered since the last call.
        // Registration may occur inside a Z3 observer callback, where creating
        // ASTs is forbidden; callers invoke this after the solver returns and
        // before the terminal run-close event.
        void validate_terms();

        // Declares a parse-local source object in the recorder's immutable source
        // snapshot. Repeated declarations of the same node return the same ID.
        std::string source_node(std::size_t parse_local_node_id, syntax::SourceSpan span, std::string_view syntax_kind);

        // Records compiler evidence relating source syntax to a live Z3 term. The
        // term is strongly registered before the edge is emitted.
        void source_term(std::size_t parse_local_node_id, syntax::SourceSpan span, std::string_view syntax_kind,
                         z3::expr const &expression, std::string_view correspondence,
                         std::vector<std::string> const &within = {});

        std::string record(std::string_view kind, std::string_view operation,
                           std::vector<std::string> const &within,
                           std::string_view producer, std::string_view coverage,
                           std::vector<RainfallField> const &data = {});

        // The qi_queue acceptance callback and the later on-clause callback are
        // separate Z3 boundaries. Pair them by exact strong term handles rather
        // than chronology, and consume each accepted instance at most once.
        void remember_quantifier_instance(std::string quantifier,
                                          std::string instance,
                                          std::string event);
        std::optional<std::string> take_quantifier_instance(
            std::string const &quantifier, std::string const &instance);

        static RainfallField string_field(std::string name, std::string_view value);
        static RainfallField number_field(std::string name, std::size_t value);
        static RainfallField boolean_field(std::string name, bool value);
        static RainfallField raw_field(std::string name, std::string json);
        static std::string quote(std::string_view text);
        static std::string string_array(std::vector<std::string> const &values);

    private:
        z3::context &context_;
        std::ostream &output_;
        std::string run_;
        SourceSnapshot const *snapshot_ = nullptr;
        std::vector<z3::expr> terms_;
        std::vector<std::string> lifted_texts_;
        std::vector<std::string> lifted_text_hashes_;
        std::size_t validated_terms_ = 0;
        std::map<std::pair<std::string, std::string>, std::string>
            pending_quantifier_instances_;
        std::map<std::size_t, std::string> source_nodes_;
        std::size_t sequence_ = 0;
    };

}  // namespace fine
