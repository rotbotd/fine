#include "synthesis.h"
#include "internal_rewrite.h"
#include "rainfall.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fine {
namespace {

struct Candidate {
    std::size_t size;
    z3::expr term;
};

class CandidateEnumerator {
public:
    CandidateEnumerator(z3::context& context,
                        std::vector<z3::expr> const& parameters)
        : context_(context) {
        by_size_.resize(2);
        for (z3::expr const& parameter : parameters) {
            if (parameter.get_sort().is_int())
                add_unique(by_size_[1], parameter);
        }
        add_unique(by_size_[1], context_.int_val(0));
        add_unique(by_size_[1], context_.int_val(1));
    }

    Candidate next() {
        for (;;) {
            ensure_size(current_size_);
            if (current_index_ < by_size_[current_size_].size())
                return {current_size_, by_size_[current_size_][current_index_++]};
            ++current_size_;
            current_index_ = 0;
        }
    }

private:
    z3::context& context_;
    std::vector<std::vector<z3::expr>> by_size_;
    std::size_t current_size_ = 1;
    std::size_t current_index_ = 0;

    void ensure_size(std::size_t size) {
        while (by_size_.size() <= size) {
            std::size_t target = by_size_.size();
            by_size_.emplace_back();
            if (target < 3) continue;
            for (std::size_t left_size = 1; left_size + 1 < target; ++left_size) {
                std::size_t right_size = target - left_size - 1;
                if (right_size == 0 || left_size >= by_size_.size() ||
                    right_size >= by_size_.size())
                    continue;
                for (z3::expr const& left : by_size_[left_size]) {
                    for (z3::expr const& right : by_size_[right_size]) {
                        add_unique(by_size_[target], left + right);
                        add_unique(by_size_[target], left - right);
                    }
                }
            }
        }
    }

    void add_unique(std::vector<z3::expr>& terms, z3::expr const& term) {
        for (z3::expr const& existing : terms) {
            if (Z3_is_eq_ast(context_, existing, term)) return;
        }
        terms.push_back(term);
    }
};

z3::expr substitute(z3::expr expression,
                    std::vector<z3::expr> const& from,
                    std::vector<z3::expr> const& to) {
    z3::context& context = expression.ctx();
    z3::expr_vector source(context);
    z3::expr_vector destination(context);
    for (z3::expr const& item : from) source.push_back(item);
    for (z3::expr const& item : to) destination.push_back(item);
    return expression.substitute(source, destination);
}

bool contains(z3::context& context, z3::expr_vector const& expressions,
              z3::expr const& sought) {
    for (z3::expr const& expression : expressions) {
        if (Z3_is_eq_ast(context, expression, sought)) return true;
    }
    return false;
}

char const* check_text(z3::check_result result) {
    if (result == z3::sat) return "sat";
    if (result == z3::unsat) return "unsat";
    return "unknown";
}

std::string ref_array(RainfallRecorder& rainfall,
                      std::vector<z3::expr> const& expressions) {
    std::vector<std::string> references;
    references.reserve(expressions.size());
    for (z3::expr const& expression : expressions)
        references.push_back(rainfall.term(expression));
    return RainfallRecorder::string_array(references);
}

std::string ref_array(RainfallRecorder& rainfall,
                      z3::expr_vector const& expressions) {
    std::vector<std::string> references;
    references.reserve(expressions.size());
    for (z3::expr const& expression : expressions)
        references.push_back(rainfall.term(expression));
    return RainfallRecorder::string_array(references);
}

std::string model_assignments(
    RainfallRecorder& rainfall, std::vector<z3::expr> const& symbols,
    std::vector<z3::expr> const& values) {
    std::string result = "[";
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (i) result += ',';
        result += "{\"symbol\":" + RainfallRecorder::quote(rainfall.term(symbols[i])) +
                  ",\"value\":" + RainfallRecorder::quote(rainfall.term(values[i])) + "}";
    }
    result += ']';
    return result;
}

} // namespace

RefutationSynthesizer::RefutationSynthesizer(
    z3::context& context, std::string declaration_name,
    std::vector<z3::expr> parameters, z3::expr result_placeholder,
    z3::expr specification, RainfallRecorder* rainfall,
    std::vector<z3::expr> grammar_inputs, bool arm_scope)
    : context_(context), declaration_name_(std::move(declaration_name)),
      parameters_(std::move(parameters)),
      result_placeholder_(std::move(result_placeholder)),
      specification_(std::move(specification)), rainfall_(rainfall),
      grammar_inputs_(std::move(grammar_inputs)), arm_scope_(arm_scope) {
    if (grammar_inputs_.empty())
        grammar_inputs_ = parameters_;
}

SynthesisResult RefutationSynthesizer::run() {
    std::string run_scope = (arm_scope_ ? "synth-arm:" : "synth:") + declaration_name_;
    if (rainfall_) {
        rainfall_->record(
            "scope", arm_scope_ ? "synth.arm.open" : "synth.run.open", {run_scope}, "fine.synthesis",
            "Fine's native QF-LIA refutation loop; excludes Z3-internal rewrites and search steps",
            {RainfallRecorder::string_field("name", declaration_name_),
             RainfallRecorder::raw_field("parameters",
                 ref_array(*rainfall_, parameters_)),
             RainfallRecorder::string_field("result",
                 rainfall_->term(result_placeholder_)),
             RainfallRecorder::string_field("specification",
                 rainfall_->term(specification_)),
             RainfallRecorder::string_field(
                 "candidate_policy",
                 "first size-stratified term whose ground coverage query is satisfiable")});
    }

    std::vector<z3::expr> counterexample_parameters;
    counterexample_parameters.reserve(parameters_.size());
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        std::string name = "Fine.synth." + declaration_name_ + ".k" +
                           std::to_string(i);
        counterexample_parameters.push_back(
            context_.constant(name.c_str(), parameters_[i].get_sort()));
    }

    CandidateEnumerator candidates(context_, grammar_inputs_);
    z3::solver gamma(context_);
    std::vector<SynthesisSelection> selections;
    z3::expr_vector active(context_);
    z3::expr_vector core(context_);
    std::size_t query_sequence = 0;
    std::string core_query;

    for (;;) {
        std::string state_query = "query:" + std::to_string(query_sequence++);
        if (rainfall_) {
            rainfall_->record(
                "scope", "solver.query.open", {run_scope, state_query},
                "fine.synthesis", "Public solver assertion and assumption boundary",
                {RainfallRecorder::string_field("id", state_query),
                 RainfallRecorder::string_field(
                     "purpose", "find a counterexample not covered by active instances"),
                 RainfallRecorder::raw_field("assumptions",
                     ref_array(*rainfall_, active)),
                 RainfallRecorder::string_field(
                     "polarity", "uncovered-counterexample-exists")});
        }
        z3::check_result state = gamma.check(active);
        if (rainfall_) {
            rainfall_->record(
                "transition", "solver.query.result", {run_scope, state_query},
                "z3.public-api", "Final public check result only; no claim about internal cause",
                {RainfallRecorder::string_field("query", state_query),
                 RainfallRecorder::string_field("status", check_text(state)),
                 RainfallRecorder::string_field(
                     "polarity", "uncovered-counterexample-exists")});
            rainfall_->record(
                "scope", "solver.query.close", {run_scope, state_query},
                "fine.synthesis", "Public solver query lifetime",
                {RainfallRecorder::string_field("id", state_query)});
        }
        if (state == z3::unsat) {
            core_query = state_query;
            core = gamma.unsat_core();
            if (rainfall_) {
                rainfall_->record(
                    "object", "solver.unsat-core", {run_scope}, "z3.public-api",
                    "Assumption core returned for the immediately preceding unsatisfiable query",
                    {RainfallRecorder::string_field("query", state_query),
                     RainfallRecorder::raw_field("members",
                         ref_array(*rainfall_, core))});
            }
            break;
        }
        if (state == z3::unknown)
            throw std::runtime_error("synthesis context was unknown: " +
                                     gamma.reason_unknown());

        Candidate selected = candidates.next();
        for (;;) {
            z3::expr candidate_at_k = substitute(
                selected.term, parameters_, counterexample_parameters);
            std::vector<z3::expr> from = parameters_;
            std::vector<z3::expr> to = counterexample_parameters;
            from.push_back(result_placeholder_);
            to.push_back(candidate_at_k);
            z3::expr instance = substitute(specification_, from, to);

            std::string selection_query =
                "query:" + std::to_string(query_sequence++);
            if (rainfall_) {
                rainfall_->record(
                    "scope", "solver.query.open", {run_scope, selection_query},
                    "fine.synthesis", "Public solver assertion and assumption boundary",
                    {RainfallRecorder::string_field("id", selection_query),
                     RainfallRecorder::string_field(
                         "purpose", "test whether a candidate covers one live counterexample"),
                     RainfallRecorder::string_field("candidate",
                         rainfall_->term(selected.term)),
                     RainfallRecorder::number_field("grammar_size", selected.size),
                     RainfallRecorder::string_field("instance",
                         rainfall_->term(instance)),
                     RainfallRecorder::raw_field("assumptions",
                         ref_array(*rainfall_, active)),
                     RainfallRecorder::string_field(
                         "polarity", "candidate-has-compatible-ground-instance")});
            }
            gamma.push();
            gamma.add(instance);
            z3::check_result useful = gamma.check(active);
            std::vector<z3::expr> model_values;
            if (useful == z3::sat) {
                z3::model model = gamma.get_model();
                model_values.reserve(counterexample_parameters.size());
                for (z3::expr const& parameter : counterexample_parameters)
                    model_values.push_back(model.eval(parameter, true));
            }
            if (rainfall_) {
                std::vector<RainfallField> fields{
                    RainfallRecorder::string_field("query", selection_query),
                    RainfallRecorder::string_field("status", check_text(useful)),
                    RainfallRecorder::string_field(
                        "polarity", "candidate-has-compatible-ground-instance")};
                if (useful == z3::sat)
                    fields.insert(fields.end(), {
                        RainfallRecorder::raw_field(
                            "model_assignments",
                            model_assignments(*rainfall_, counterexample_parameters,
                                              model_values)),
                        RainfallRecorder::boolean_field("model_completion", true),
                        RainfallRecorder::string_field(
                            "relation", "equality-under-this-model")});
                rainfall_->record(
                    "transition", "solver.query.result",
                    {run_scope, selection_query}, "z3.public-api",
                    "Final public check result and completed values for Fine's counterexample symbols",
                    fields);
                rainfall_->record(
                    "scope", "solver.query.close", {run_scope, selection_query},
                    "fine.synthesis", "Public solver query lifetime",
                    {RainfallRecorder::string_field("id", selection_query)});
            }
            gamma.pop();
            if (useful == z3::unknown)
                throw std::runtime_error("candidate selection was unknown: " +
                                         gamma.reason_unknown());
            if (useful == z3::sat) {
                if (rainfall_) {
                    rainfall_->record(
                        "derive", "synth.candidate.select", {run_scope},
                        "fine.synthesis",
                        "Deterministic first satisfiable candidate in exact-size enumeration order",
                        {RainfallRecorder::string_field("candidate",
                             rainfall_->term(selected.term)),
                         RainfallRecorder::number_field("grammar_size", selected.size),
                         RainfallRecorder::string_field("evidence_query",
                             selection_query),
                         RainfallRecorder::string_field(
                             "policy", "first-enumerated-with-satisfiable-coverage")});
                }
                std::string label_name = "Fine.synth." + declaration_name_ +
                                         ".instance" +
                                         std::to_string(selections.size());
                z3::expr label = context_.bool_const(label_name.c_str());
                gamma.add(z3::implies(label, !instance));
                active.push_back(label);
                selections.push_back(
                    {selected.size, selected.term, instance, label});
                if (rainfall_) {
                    rainfall_->record(
                        "constraint", "synth.instance.activate", {run_scope},
                        "fine.synthesis", "Labelled negated ground instances asserted into gamma",
                        {RainfallRecorder::string_field("label",
                             rainfall_->term(label)),
                         RainfallRecorder::string_field("instance",
                             rainfall_->term(instance)),
                         RainfallRecorder::string_field("candidate",
                             rainfall_->term(selected.term)),
                         RainfallRecorder::string_field("evidence_query",
                             selection_query)});
                }
                break;
            }
            if (rainfall_) {
                rainfall_->record(
                    "derive", "synth.candidate.reject", {run_scope},
                    "fine.synthesis",
                    "Candidate rejected only by its unsatisfiable ground coverage query",
                    {RainfallRecorder::string_field("candidate",
                         rainfall_->term(selected.term)),
                     RainfallRecorder::number_field("grammar_size", selected.size),
                     RainfallRecorder::string_field("evidence_query", selection_query)});
            }
            selected = candidates.next();
        }
    }

    std::vector<std::size_t> core_indices;
    for (std::size_t i = 0; i < selections.size(); ++i) {
        if (contains(context_, core, selections[i].label))
            core_indices.push_back(i);
    }
    if (core_indices.empty())
        throw std::runtime_error("synthesis refutation produced an empty core");

    z3::expr assembled = selections[core_indices.front()].term;
    for (std::size_t position = 1; position < core_indices.size(); ++position) {
        z3::expr term = selections[core_indices[position]].term;
        std::vector<z3::expr> from{result_placeholder_};
        std::vector<z3::expr> to{term};
        z3::expr condition = substitute(specification_, from, to);
        assembled = z3::ite(condition, term, assembled);
    }
    if (rainfall_) {
        std::vector<z3::expr> core_terms;
        for (std::size_t index : core_indices)
            core_terms.push_back(selections[index].term);
        rainfall_->record(
            "derive", "synth.assemble-core", {run_scope}, "fine.synthesis",
            "Insertion-ordered unsat-core members assembled with Fine's Reynolds-style conditional recipe",
            {RainfallRecorder::raw_field("inputs",
                 ref_array(*rainfall_, core_terms)),
             RainfallRecorder::string_field("output", rainfall_->term(assembled)),
             RainfallRecorder::number_field("core_size", core_indices.size()),
             RainfallRecorder::string_field("evidence_query", core_query)});
    }
    z3::expr witness = simplify_with_rainfall(
        assembled, rainfall_, {run_scope, "transform:public-simplify"});
    if (rainfall_) {
        rainfall_->record(
            "transform", "z3.simplify", {run_scope}, "z3.public-api",
            "One public simplify call; excludes the internal rewrite sequence",
            {RainfallRecorder::string_field("before", rainfall_->term(assembled)),
             RainfallRecorder::string_field("after", rainfall_->term(witness)),
             RainfallRecorder::string_field("relation", "theory-equivalent")});
    }

    std::vector<z3::expr> from{result_placeholder_};
    std::vector<z3::expr> to{witness};
    z3::expr verified_specification = substitute(specification_, from, to);
    z3::solver verifier(context_);
    verifier.add(!verified_specification);
    std::string verification_query =
        "query:" + std::to_string(query_sequence++);
    if (rainfall_) {
        rainfall_->record(
            "scope", "solver.query.open", {run_scope, verification_query},
            "fine.synthesis", "Fresh solver containing only the negated original specification",
            {RainfallRecorder::string_field("id", verification_query),
             RainfallRecorder::string_field(
                 "purpose", "independently search for a witness counterexample"),
             RainfallRecorder::string_field("candidate", rainfall_->term(witness)),
             RainfallRecorder::string_field("assertion",
                 rainfall_->term(!verified_specification)),
             RainfallRecorder::string_field("polarity", "counterexample-exists")});
    }
    z3::check_result verification = verifier.check();
    if (rainfall_) {
        rainfall_->record(
            "transition", "solver.query.result",
            {run_scope, verification_query}, "z3.public-api",
            "Final public check result only; independent of synthesis gamma",
            {RainfallRecorder::string_field("query", verification_query),
             RainfallRecorder::string_field("status", check_text(verification)),
             RainfallRecorder::string_field("polarity", "counterexample-exists"),
             RainfallRecorder::string_field(
                 "domain_outcome",
                 verification == z3::unsat ? "verified" : "not-verified")});
        rainfall_->record(
            "scope", "solver.query.close", {run_scope, verification_query},
            "fine.synthesis", "Fresh verification solver query lifetime",
            {RainfallRecorder::string_field("id", verification_query)});
    }
    if (verification == z3::unknown)
        throw std::runtime_error("synthesized candidate verification was unknown: " +
                                 verifier.reason_unknown());
    if (verification != z3::unsat)
        throw std::runtime_error("synthesized candidate failed independent verification");

    if (rainfall_) {
        rainfall_->record(
            "transition", "synth.backend.accept", {run_scope}, "fine.synthesis",
            "Semantic candidate accepted after a fresh unsatisfiable counterexample query",
            {RainfallRecorder::string_field("candidate", rainfall_->term(witness)),
             RainfallRecorder::string_field("evidence_query", verification_query),
             RainfallRecorder::string_field("status", "verified")});
    }

    SynthesisResult result(assembled, witness);
    result.selections = std::move(selections);
    result.core_indices = std::move(core_indices);
    return result;
}

} // namespace fine
