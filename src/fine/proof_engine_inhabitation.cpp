#include "elaboration_internal.h"

// Exact and conservative inhabitation summaries for static indexed evidence.
namespace fine::elaboration {

    std::vector<z3::expr> ProofEngine::constructor_identity_constraints(syntax::ProofConstructorDecl const &constructor,
                                                                        ValueEnvironment const &constructor_values) {
        std::vector<z3::expr> constraints;
        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        auto collect = [&](syntax::CoeffectParameter const &parameter) {
            if (parameter.type.kind != syntax::ProofType::Kind::identity)
                return;
            IdentityType identity =
                elaborate_identity(parameter.type, constructor_values, no_proofs, no_proof_order, no_absorbed);
            constraints.push_back(identity.left == identity.right);
        };
        for (auto const &parameter : constructor.explicit_proof_parameters)
            collect(parameter);
        for (auto const &parameter : constructor.proof_parameters)
            collect(parameter);
        return constraints;
    }

    bool ProofEngine::proof_family_has_finite_constructor_tree(std::string const &family) const {
        if (!proof_inductives_.contains(family))
            throw std::logic_error("proof premise names an undeclared family");

        std::set<std::string> grounded;
        bool changed;
        do {
            changed = false;
            for (auto const &[name, declaration] : proof_inductives_) {
                if (grounded.contains(name))
                    continue;
                for (auto const &constructor : declaration->constructors) {
                    bool premises_grounded = true;
                    auto inspect = [&](syntax::CoeffectParameter const &parameter) {
                        if (parameter.type.kind == syntax::ProofType::Kind::inductive &&
                            !grounded.contains(parameter.type.name))
                            premises_grounded = false;
                    };
                    for (auto const &parameter : constructor.explicit_proof_parameters)
                        inspect(parameter);
                    for (auto const &parameter : constructor.proof_parameters)
                        inspect(parameter);
                    if (premises_grounded) {
                        grounded.insert(name);
                        changed = true;
                        break;
                    }
                }
            }
        } while (changed);
        return grounded.contains(family);
    }

    std::optional<ProofEngine::FiniteInhabitation> ProofEngine::finite_inhabitation(std::string const &family) {
        if (auto cached = finite_inhabitation_cache_.find(family); cached != finite_inhabitation_cache_.end())
            return cached->second;
        auto family_found = proof_inductives_.find(family);
        if (family_found == proof_inductives_.end())
            throw std::logic_error("proof premise names an undeclared family");

        // Install a conservative sentinel before following premise families. Fine currently
        // permits self recursion and backward references; the sentinel also makes a future
        // mutually recursive declaration group fall back instead of recursing in the host.
        finite_inhabitation_cache_.emplace(family, std::nullopt);
        syntax::ProofInductiveDecl const &declaration = *family_found->second;
        std::vector<std::vector<z3::expr>> domain(1);
        constexpr std::size_t max_finite_states = 256;
        for (auto const &index : declaration.indices) {
            auto values = values_.finite_values(kind_of(index.type));
            if (!values || values->empty() || domain.size() > max_finite_states / values->size())
                return std::nullopt;
            std::vector<std::vector<z3::expr>> product;
            product.reserve(domain.size() * values->size());
            for (auto const &prefix : domain)
                for (auto const &value : *values) {
                    auto tuple = prefix;
                    tuple.push_back(value);
                    product.push_back(std::move(tuple));
                }
            domain = std::move(product);
        }

        auto tuple_member = [&](std::vector<ValueTerm> const &indices,
                                std::vector<std::vector<z3::expr>> const &members) {
            z3::expr result = values_.context().bool_val(false);
            for (auto const &member : members) {
                if (member.size() != indices.size())
                    throw std::logic_error("finite proof-family state has the wrong arity");
                z3::expr equal = values_.context().bool_val(true);
                for (std::size_t i = 0; i < member.size(); ++i)
                    equal = equal && indices[i].expression == member[i];
                result = result || equal;
            }
            return result;
        };
        auto already_reached = [&](std::vector<z3::expr> const &candidate,
                                   std::vector<std::vector<z3::expr>> const &reached) {
            return std::any_of(reached.begin(), reached.end(), [&](auto const &existing) {
                if (existing.size() != candidate.size())
                    return false;
                for (std::size_t i = 0; i < existing.size(); ++i)
                    if (!same_ast(values_.context(), existing[i], candidate[i]))
                        return false;
                return true;
            });
        };

        FiniteInhabitation result;
        bool changed;
        do {
            changed = false;
            auto previous = result.reachable_indices;
            for (std::size_t state_index = 0; state_index < domain.size(); ++state_index) {
                auto const &state = domain[state_index];
                if (already_reached(state, previous))
                    continue;
                for (auto const &constructor : declaration.constructors) {
                    ValueEnvironment constructor_values;
                    for (auto const &parameter : constructor.parameters) {
                        ValueKind kind = kind_of(parameter.type);
                        std::string symbol = "fine.proof-finite." + family + "." +
                                             std::to_string(result.rounds) + "." + std::to_string(state_index) + "." +
                                             constructor.name + "." + parameter.name;
                        constructor_values.emplace(
                            parameter.name,
                            ValueTerm(kind, values_.context().constant(symbol.c_str(), values_.sort(kind))));
                    }
                    ProofEnvironment no_proofs;
                    std::vector<std::string> no_proof_order;
                    std::vector<z3::expr> no_absorbed;
                    SemanticProofType constructor_result =
                        elaborate_proof_type(constructor.result_type, constructor_values, no_proofs, no_proof_order,
                                             no_absorbed);
                    auto result_type = std::get_if<InductiveType>(&constructor_result);
                    if (!result_type || result_type->family != family || result_type->indices.size() != state.size())
                        throw std::logic_error("checked proof constructor changed family or arity");
                    z3::expr condition = values_.context().bool_val(true);
                    for (std::size_t i = 0; i < state.size(); ++i)
                        condition = condition && result_type->indices[i].expression == state[i];
                    for (auto const &constraint : constructor_identity_constraints(constructor, constructor_values))
                        condition = condition && constraint;

                    bool exact = true;
                    auto require_premise = [&](syntax::CoeffectParameter const &parameter) {
                        if (!exact || parameter.type.kind != syntax::ProofType::Kind::inductive)
                            return;
                        SemanticProofType premise = elaborate_proof_type(parameter.type, constructor_values, no_proofs,
                                                                        no_proof_order, no_absorbed);
                        auto premise_type = std::get_if<InductiveType>(&premise);
                        if (!premise_type)
                            throw std::logic_error("indexed constructor premise changed proof kind");
                        if (premise_type->family == family) {
                            condition = condition && tuple_member(premise_type->indices, previous);
                            return;
                        }
                        auto premise_states = finite_inhabitation(premise_type->family);
                        if (!premise_states) {
                            exact = false;
                            return;
                        }
                        condition = condition &&
                                    tuple_member(premise_type->indices, premise_states->reachable_indices);
                    };
                    for (auto const &parameter : constructor.explicit_proof_parameters)
                        require_premise(parameter);
                    for (auto const &parameter : constructor.proof_parameters)
                        require_premise(parameter);
                    if (!exact)
                        return std::nullopt;

                    z3::solver solver(values_.context());
                    solver.add(condition);
                    z3::check_result status = solver.check();
                    if (status == z3::unknown)
                        return std::nullopt;
                    if (status == z3::sat) {
                        result.reachable_indices.push_back(state);
                        changed = true;
                        break;
                    }
                }
            }
            ++result.rounds;
        } while (changed);
        finite_inhabitation_cache_[family] = result;
        if (rainfall_)
            rainfall_->record(
                "derive", "proof.inductive.finite-inhabitation", {"proof-inductive:" + family},
                "fine.proof-elaborator",
                "Fine computes the exact least constructor closure when every proof index has a small finite value "
                "domain",
                {RainfallRecorder::string_field("family", family),
                 RainfallRecorder::number_field("domain_states", domain.size()),
                 RainfallRecorder::number_field("reachable_states", result.reachable_indices.size()),
                 RainfallRecorder::number_field("rounds", result.rounds),
                 RainfallRecorder::number_field("state_cap", max_finite_states),
                 RainfallRecorder::boolean_field("least_fixed_point", true)});
        return result;
    }

    std::optional<z3::expr> ProofEngine::finite_inductive_head_cover(InductiveType const &type) {
        auto inhabitation = finite_inhabitation(type.family);
        if (!inhabitation)
            return std::nullopt;
        z3::expr cover = values_.context().bool_val(false);
        for (auto const &state : inhabitation->reachable_indices) {
            if (state.size() != type.indices.size())
                throw std::logic_error("finite proof-family state has the wrong arity");
            z3::expr equal = values_.context().bool_val(true);
            for (std::size_t i = 0; i < state.size(); ++i)
                equal = equal && type.indices[i].expression == state[i];
            cover = cover || equal;
        }
        return cover.simplify();
    }

    ProofEngine::IndexedPremiseShape ProofEngine::constructor_indexed_premise_shape(
        syntax::ProofConstructorDecl const &constructor, ValueEnvironment const &constructor_values,
        std::string const &evidence_name, std::set<std::string> &expanding) {
        IndexedPremiseShape shape;
        auto inspect = [&](syntax::CoeffectParameter const &parameter) {
            if (parameter.type.kind != syntax::ProofType::Kind::inductive)
                return;
            ++shape.total;
            if (!proof_family_has_finite_constructor_tree(parameter.type.name))
                ++shape.impossible;
            ProofEnvironment no_proofs;
            std::vector<std::string> no_proof_order;
            std::vector<z3::expr> no_absorbed;
            SemanticProofType premise = elaborate_proof_type(parameter.type, constructor_values, no_proofs,
                                                             no_proof_order, no_absorbed);
            auto inductive = std::get_if<InductiveType>(&premise);
            if (!inductive)
                throw std::logic_error("indexed constructor premise changed proof kind");
            if (auto finite_cover = finite_inductive_head_cover(*inductive)) {
                shape.covers.push_back(std::move(*finite_cover));
                ++shape.expanded;
                return;
            }
            if (expanding.contains(parameter.type.name))
                return;
            shape.covers.push_back(
                inductive_head_cover(*inductive, evidence_name + "." + parameter.name, expanding));
            ++shape.expanded;
        };
        for (auto const &parameter : constructor.explicit_proof_parameters)
            inspect(parameter);
        for (auto const &parameter : constructor.proof_parameters)
            inspect(parameter);
        return shape;
    }

    z3::expr ProofEngine::inductive_head_cover(InductiveType const &type, std::string const &evidence_name) {
        std::set<std::string> expanding;
        return inductive_head_cover(type, evidence_name, expanding);
    }
    z3::expr ProofEngine::inductive_head_cover(InductiveType const &type, std::string const &evidence_name,
                                               std::set<std::string> &expanding) {
        if (auto finite_cover = finite_inductive_head_cover(type))
            return std::move(*finite_cover);
        auto family_found = proof_inductives_.find(type.family);
        if (family_found == proof_inductives_.end())
            throw std::logic_error("proof evidence names an undeclared family");
        if (!expanding.insert(type.family).second)
            return values_.context().bool_val(true);
        z3::expr cover = values_.context().bool_val(false);
        for (auto const &constructor : family_found->second->constructors) {
            ValueEnvironment constructor_values;
            z3::expr_vector witnesses(values_.context());
            for (auto const &parameter : constructor.parameters) {
                ValueKind kind = kind_of(parameter.type);
                std::string symbol = "fine.proof-head." + evidence_name + "." + constructor.name + "." + parameter.name;
                z3::expr witness = values_.context().constant(symbol.c_str(), values_.sort(kind));
                witnesses.push_back(witness);
                constructor_values.emplace(parameter.name, ValueTerm(kind, std::move(witness)));
            }
            ProofEnvironment no_proofs;
            std::vector<std::string> no_proof_order;
            std::vector<z3::expr> no_absorbed;
            SemanticProofType result = elaborate_proof_type(constructor.result_type, constructor_values, no_proofs,
                                                            no_proof_order, no_absorbed);
            auto result_type = std::get_if<InductiveType>(&result);
            if (!result_type || result_type->family != type.family ||
                result_type->indices.size() != type.indices.size())
                throw std::logic_error("checked proof constructor changed family or arity");
            z3::expr head = values_.context().bool_val(true);
            for (std::size_t i = 0; i < type.indices.size(); ++i)
                head = head && result_type->indices[i].expression == type.indices[i].expression;
            for (auto const &constraint : constructor_identity_constraints(constructor, constructor_values))
                head = head && constraint;
            IndexedPremiseShape indexed_premises = constructor_indexed_premise_shape(
                constructor, constructor_values, "fine.proof-head." + evidence_name + "." + constructor.name,
                expanding);
            for (auto const &premise_cover : indexed_premises.covers)
                head = head && premise_cover;
            if (indexed_premises.impossible != 0)
                head = head && values_.context().bool_val(false);
            if (!witnesses.empty())
                head = z3::exists(witnesses, head);
            cover = cover || head;
        }
        expanding.erase(type.family);
        return cover.simplify();
    }
}  // namespace fine::elaboration
