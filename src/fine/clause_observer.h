#pragma once

#include "c++/z3++.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fine {

class RainfallRecorder;

// Retains Z3's public on-clause registration for exactly one solver lifetime.
// The callback reports the clause stream admitted to CDCL(T) after
// preprocessing. It does not expose rejected candidates, assignments, or the
// causal contribution of an individual clause to the final result.
class RainfallClauseObserver {
public:
    RainfallClauseObserver(z3::solver& solver, RainfallRecorder& rainfall,
                           std::vector<std::string> within);

private:
    void on_clause(z3::expr const& proof_hint,
                   std::vector<unsigned> const& dependencies,
                   z3::expr_vector const& clause);

    RainfallRecorder& rainfall_;
    std::vector<std::string> within_;
    z3::on_clause_eh_t callback_;
    std::unique_ptr<z3::on_clause> registration_;
};

} // namespace fine
