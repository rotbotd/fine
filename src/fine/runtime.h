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

    class LiveLiftPipeline;
    struct ExecutionResult;
    namespace elaboration {
        class ValueElaborator;
    }
    namespace stage {
        class CertifiedValueFlowProgram;
        CertifiedValueFlowProgram build_certified_value_flow(syntax::Document const &document,
                                                              ExecutionResult const &execution);
    }

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

    // Evidence that one exact source SCC passed Fine's size-change check before
    // any member was installed as a native recursive definition. Construction
    // is private: staging may consume this result, but it cannot manufacture
    // termination permission from a detached call graph.
    class ValueRecursionCertificate {
    public:
        std::vector<std::string> const &functions() const noexcept {
            return functions_;
        }
        std::size_t call_graphs() const noexcept {
            return call_graphs_;
        }
        std::size_t closure_graphs() const noexcept {
            return closure_graphs_;
        }
        std::size_t idempotent_loops() const noexcept {
            return idempotent_loops_;
        }

    private:
        friend class elaboration::ValueElaborator;
        friend stage::CertifiedValueFlowProgram
        stage::build_certified_value_flow(syntax::Document const &, ExecutionResult const &);

        ValueRecursionCertificate(std::vector<syntax::FunctionDecl const *> declarations, std::size_t call_graphs,
                                  std::size_t closure_graphs, std::size_t idempotent_loops);

        // These identities are never dereferenced by staging. They make a
        // certificate valid only for the parsed declarations which were
        // actually elaborated, rather than another document with the same
        // spellings.
        std::vector<syntax::FunctionDecl const *> declarations_;
        std::vector<std::string> functions_;
        std::size_t call_graphs_ = 0;
        std::size_t closure_graphs_ = 0;
        std::size_t idempotent_loops_ = 0;
    };

    struct ExecutionResult {
        std::vector<Materialization> materializations;
        std::vector<ValueRecursionCertificate> value_recursion_certificates;
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
        bool live_iterative_proof_search = false;
        std::size_t live_proof_search_start = 1;
        // Zero leaves the iterative deepening loop to its owning process.
        std::size_t live_proof_search_limit = 0;
        LiveLiftPipeline *live_lift = nullptr;
    };

    ExecutionResult execute(syntax::Document const &document, std::ostream &output,
                            std::ostream *rainfall_output = nullptr, SourceSnapshot const *snapshot = nullptr,
                            std::string rainfall_run = {}, ExecutionOptions options = {});

    std::string apply_materializations(syntax::ConcreteSyntaxTree const &tree,
                                       std::vector<Materialization> materializations);

}  // namespace fine
