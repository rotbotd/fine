#pragma once

#include "parser.h"

#include <iosfwd>
#include <string>

namespace fine::stage {

    // Evaluate one source-named, nullary value function through the certified
    // staging transfer. A nullary wrapper is the source-level exact-input
    // request: its body contains the arguments in ordinary Fine syntax.
    int run_stage_diagnostic(syntax::Document const &document, std::string const &function,
                             std::ostream &output);
    // Evaluate the certified transfer with every formal parameter at runtime,
    // then replace each outermost exact, recursion-safe expression island. A
    // nullary exact body remains the whole-body case.
    std::string materialize_stage_result(syntax::ConcreteSyntaxTree const &tree, std::string const &function,
                                         std::ostream &failure_output);

}  // namespace fine::stage
