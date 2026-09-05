#pragma once

#include "parser.h"

#include <cstdint>
#include <vector>

namespace fine::elaboration {

    enum class SizeRelation : std::uint8_t { unknown, nonincreasing, decreasing };

    struct SizeChangeCall {
        syntax::FunctionDecl const *caller;
        syntax::FunctionDecl const *callee;
        syntax::SourceSpan span;
        // Row: caller parameter. Column: callee parameter.
        std::vector<std::vector<SizeRelation>> relation;
    };

    struct SizeChangeSummary {
        std::size_t call_graphs;
        std::size_t closure_graphs;
        std::size_t idempotent_loops;
    };

    SizeChangeSummary require_size_change_termination(std::vector<syntax::FunctionDecl const *> const &group,
                                                      std::vector<SizeChangeCall> const &calls);

}  // namespace fine::elaboration
