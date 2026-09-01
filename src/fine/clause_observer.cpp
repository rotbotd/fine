#include "clause_observer.h"

#include "rainfall.h"

#include <sstream>
#include <stdexcept>
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

    std::vector<RainfallField> fields{
        RainfallRecorder::string_field("action", action),
        RainfallRecorder::string_field(
            "proof_hint",
            rainfall_.term(proof_hint, "z3-clause-proof-hint")),
        RainfallRecorder::string_field("proof_hint_head", hint_head),
        RainfallRecorder::raw_field(
            "literals", RainfallRecorder::string_array(literal_references)),
        RainfallRecorder::number_field("literal_count", clause.size()),
        RainfallRecorder::raw_field(
            "dependency_indices", number_array(dependencies))};

    // With clause proofs active, qi_queue constructs `(inst q (not body)
    // (bind ...) (gen ...))` from the same q and unsimplified ground body seen
    // by on_binding. Join the two public observations through their exact
    // recorder handles. Merely being the next event is not evidence.
    if (hint_head == "inst" && proof_hint.num_args() >= 3) {
        z3::expr quantifier = proof_hint.arg(0);
        z3::expr negated_instance = proof_hint.arg(1);
        z3::expr bindings = proof_hint.arg(2);
        if (!negated_instance.is_not() || negated_instance.num_args() != 1 ||
            !bindings.is_app() || bindings.decl().name().str() != "bind")
            throw std::runtime_error("unexpected Z3 inst proof-hint shape");
        z3::expr instance = negated_instance.arg(0);
        std::string quantifier_reference = rainfall_.term(quantifier);
        std::string instance_reference = rainfall_.term(instance);
        std::optional<std::string> accepted = rainfall_.take_quantifier_instance(
            quantifier_reference, instance_reference);
        if (!accepted)
            throw std::runtime_error(
                "quantifier lemma has no exact accepted-instance evidence");

        std::vector<std::string> binding_references;
        binding_references.reserve(bindings.num_args());
        for (unsigned i = 0; i < bindings.num_args(); ++i)
            binding_references.push_back(rainfall_.term(bindings.arg(i)));
        fields.push_back(RainfallRecorder::string_field(
            "quantifier_instance_event", *accepted));
        fields.push_back(RainfallRecorder::string_field(
            "quantifier", quantifier_reference));
        fields.push_back(RainfallRecorder::string_field(
            "instance", instance_reference));
        fields.push_back(RainfallRecorder::raw_field(
            "ground_bindings",
            RainfallRecorder::string_array(binding_references)));
        fields.push_back(RainfallRecorder::string_field(
            "relation", "accepted-instance-became-admitted-clause"));
    }

    rainfall_.record(
        kind, operation, within_, "z3.solver.on_clause",
        "Clauses admitted to or removed from CDCL(T) through Z3's public on-clause callback after preprocessing; inst proof hints are joined to the exact accepted qi_queue event and ground terms, but rejected candidates, assignments, decisions, watched-literal activity, and causal contribution to the final result remain outside this boundary",
        fields);
}

} // namespace fine
