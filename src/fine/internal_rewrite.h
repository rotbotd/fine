#pragma once

#include "c++/z3++.h"

#include <string>
#include <vector>

namespace fine {

class RainfallRecorder;

// Runs the ordinary builtin theory rewriter. With a recorder, the soft-fork
// observer exposes each successful application reduction before returning the
// same root result; without one this uses the public z3++ simplify operation.
z3::expr simplify_with_rainfall(
    z3::expr const& expression, RainfallRecorder* rainfall,
    std::vector<std::string> const& within);

} // namespace fine
