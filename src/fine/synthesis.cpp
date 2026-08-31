#include "synthesis.h"

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
        for (z3::expr const& parameter : parameters)
            add_unique(by_size_[1], parameter);
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

} // namespace

RefutationSynthesizer::RefutationSynthesizer(
    z3::context& context, std::string declaration_name,
    std::vector<z3::expr> parameters, z3::expr result_placeholder,
    z3::expr specification)
    : context_(context), declaration_name_(std::move(declaration_name)),
      parameters_(std::move(parameters)),
      result_placeholder_(std::move(result_placeholder)),
      specification_(std::move(specification)) {}

SynthesisResult RefutationSynthesizer::run() {
    std::vector<z3::expr> counterexample_parameters;
    counterexample_parameters.reserve(parameters_.size());
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        std::string name = "Fine.synth." + declaration_name_ + ".k" +
                           std::to_string(i);
        counterexample_parameters.push_back(
            context_.constant(name.c_str(), parameters_[i].get_sort()));
    }

    CandidateEnumerator candidates(context_, parameters_);
    z3::solver gamma(context_);
    std::vector<SynthesisSelection> selections;
    z3::expr_vector active(context_);
    z3::expr_vector core(context_);

    for (;;) {
        z3::check_result state = gamma.check(active);
        if (state == z3::unsat) {
            core = gamma.unsat_core();
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

            gamma.push();
            gamma.add(instance);
            z3::check_result useful = gamma.check(active);
            gamma.pop();
            if (useful == z3::unknown)
                throw std::runtime_error("candidate selection was unknown: " +
                                         gamma.reason_unknown());
            if (useful == z3::sat) {
                std::string label_name = "Fine.synth." + declaration_name_ +
                                         ".instance" +
                                         std::to_string(selections.size());
                z3::expr label = context_.bool_const(label_name.c_str());
                gamma.add(z3::implies(label, !instance));
                active.push_back(label);
                selections.push_back(
                    {selected.size, selected.term, instance, label});
                break;
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
    z3::expr witness = assembled.simplify();

    std::vector<z3::expr> from{result_placeholder_};
    std::vector<z3::expr> to{witness};
    z3::expr verified_specification = substitute(specification_, from, to);
    z3::solver verifier(context_);
    verifier.add(!verified_specification);
    z3::check_result verification = verifier.check();
    if (verification == z3::unknown)
        throw std::runtime_error("synthesized candidate verification was unknown: " +
                                 verifier.reason_unknown());
    if (verification != z3::unsat)
        throw std::runtime_error("synthesized candidate failed independent verification");

    SynthesisResult result(assembled, witness);
    result.selections = std::move(selections);
    result.core_indices = std::move(core_indices);
    return result;
}

} // namespace fine
