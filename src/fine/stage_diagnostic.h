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

}  // namespace fine::stage
