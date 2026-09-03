#pragma once

#include "parser.h"
#include "source.h"

#include <cstddef>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fine {

    class SemanticError : public std::runtime_error {
    public:
        SemanticError(syntax::SourceSpan span, std::string message);

        syntax::SourceSpan span() const noexcept {
            return span_;
        }
        std::string format(std::string_view filename, std::string_view source) const;

    private:
        syntax::SourceSpan span_;
    };

    struct Materialization {
        syntax::ConcreteRange range;
        std::string text;
    };

    struct ExecutionResult {
        std::vector<Materialization> materializations;
        std::size_t functions_verified = 0;
        std::size_t proof_functions_verified = 0;
        std::size_t proofs_formed = 0;
        std::size_t proof_holes_filled = 0;
        std::size_t proof_holes_checkpointed = 0;
        std::size_t coeffects_resolved = 0;
        bool checkpoint_open = false;
    };

    enum class ProofSelector { deterministic, z3_model };

    struct ExecutionOptions {
        bool require_explicit_coeffects = false;
        bool require_materialized_proofs = false;
        bool synthesize_partial_proofs = false;
        bool validate_partial_proofs = false;
        std::size_t proof_search_cost = 3;
        ProofSelector proof_selector = ProofSelector::deterministic;
    };

    ExecutionResult execute(syntax::Document const &document, std::ostream &output,
                            std::ostream *rainfall_output = nullptr, SourceSnapshot const *snapshot = nullptr,
                            std::string rainfall_run = {}, ExecutionOptions options = {});

    std::string apply_materializations(syntax::ConcreteSyntaxTree const &tree,
                                       std::vector<Materialization> materializations);

}  // namespace fine
