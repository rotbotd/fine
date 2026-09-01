#include "clause_observer.h"

#include "rainfall.h"

#include <sstream>
#include <utility>

namespace fine {
namespace {

std::string number_array(std::vector<unsigned> const& values) {
    std::ostringstream result;
    result << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) result << ',';
        result << values[i];
    }
    result << ']';
    return result.str();
}

} // namespace

RainfallClauseObserver::RainfallClauseObserver(
    z3::solver& solver, RainfallRecorder& rainfall,
    std::vector<std::string> within)
    : rainfall_(rainfall), within_(std::move(within)) {
    callback_ = [this](z3::expr const& proof_hint,
                       std::vector<unsigned> const& dependencies,
                       z3::expr_vector const& clause) {
        on_clause(proof_hint, dependencies, clause);
    };
    registration_ = std::make_unique<z3::on_clause>(solver, callback_);
}

void RainfallClauseObserver::on_clause(
    z3::expr const& proof_hint, std::vector<unsigned> const& dependencies,
    z3::expr_vector const& clause) {
    std::string hint_head = proof_hint.decl().name().str();
    std::string operation;
    std::string kind;
    std::string action;
    if (hint_head == "assumption") {
        operation = "z3.clause.assume";
        kind = "constraint";
        action = "assumed";
    } else if (hint_head == "del") {
        operation = "z3.clause.delete";
        kind = "transition";
        action = "deleted";
    } else {
        operation = "z3.clause.infer";
        kind = "derive";
        action = "inferred";
    }

    std::vector<std::string> literal_references;
    literal_references.reserve(clause.size());
    for (z3::expr const& literal : clause)
        literal_references.push_back(
            rainfall_.term(literal, "z3-cdcl-clause-literal"));

    rainfall_.record(
        kind, operation, within_, "z3.solver.on_clause",
        "Clauses admitted to or removed from CDCL(T) through Z3's public on-clause callback after preprocessing; excludes rejected candidates, assignments, decisions, watched-literal activity, and any claim that one clause caused the final result",
        {RainfallRecorder::string_field("action", action),
         RainfallRecorder::string_field(
             "proof_hint",
             rainfall_.term(proof_hint, "z3-clause-proof-hint")),
         RainfallRecorder::string_field("proof_hint_head", hint_head),
         RainfallRecorder::raw_field(
             "literals", RainfallRecorder::string_array(literal_references)),
         RainfallRecorder::number_field("literal_count", clause.size()),
         RainfallRecorder::raw_field(
             "dependency_indices", number_array(dependencies))});
}

} // namespace fine
