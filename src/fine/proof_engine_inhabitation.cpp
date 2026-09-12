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
        // This is a latency guard, not a logical boundary. The checked chain
        // profile keeps 64 states below one second in both native and ordinary
        // Wasm builds; larger finite products retain the conservative analysis.
        constexpr std::size_t max_finite_states = 64;
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
            auto previous = result.reachable_indices;
            std::vector<std::vector<z3::expr>> additions;
            for (auto const &constructor : declaration.constructors) {
                ValueEnvironment constructor_values;
                for (auto const &parameter : constructor.parameters) {
                    ValueKind kind = kind_of(parameter.type);
                    std::string symbol = "fine.proof-finite." + family + "." + std::to_string(result.rounds) + "." +
                                         constructor.name + "." + parameter.name;
                    constructor_values.emplace(
                        parameter.name,
                        ValueTerm(kind, values_.context().constant(symbol.c_str(), values_.sort(kind))));
                }
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                SemanticProofType constructor_result = elaborate_proof_type(constructor.result_type, constructor_values,
                                                                            no_proofs, no_proof_order, no_absorbed);
                auto result_type = std::get_if<InductiveType>(&constructor_result);
                if (!result_type || result_type->family != family ||
                    result_type->indices.size() != declaration.indices.size())
                    throw std::logic_error("checked proof constructor changed family or arity");
                z3::expr condition = values_.context().bool_val(true);
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
                    condition = condition && tuple_member(premise_type->indices, premise_states->reachable_indices);
                };
                for (auto const &parameter : constructor.explicit_proof_parameters)
                    require_premise(parameter);
                for (auto const &parameter : constructor.proof_parameters)
                    require_premise(parameter);
                if (!exact)
                    return std::nullopt;

                // One incremental solver enumerates this constructor's previously unseen
                // result tuples. Rebuilding the constructor and a solver for every possible
                // target made a length-N chain cubic in its finite domain.
                condition = condition && !tuple_member(result_type->indices, previous);
                z3::solver solver(values_.context());
                solver.add(condition);
                while (true) {
                    ++result.solver_checks;
                    z3::check_result status = solver.check();
                    if (status == z3::unknown)
                        return std::nullopt;
                    if (status == z3::unsat)
                        break;
                    z3::model model = solver.get_model();
                    std::vector<z3::expr> output;
                    output.reserve(result_type->indices.size());
                    for (auto const &index : result_type->indices)
                        output.push_back(model.eval(index.expression, true).simplify());
                    auto state = std::find_if(domain.begin(), domain.end(), [&](auto const &candidate) {
                        return candidate.size() == output.size() &&
                               std::equal(candidate.begin(), candidate.end(), output.begin(),
                                          [&](auto const &left, auto const &right) {
                                              return same_ast(values_.context(), left, right);
                                          });
                    });
                    if (state == domain.end())
                        throw std::logic_error("finite proof-family model escaped its declared index domain");
                    if (!already_reached(*state, additions))
                        additions.push_back(*state);
                    z3::expr different = values_.context().bool_val(false);
                    for (std::size_t i = 0; i < state->size(); ++i)
                        different = different || result_type->indices[i].expression != state->at(i);
                    solver.add(different);
                }
            }
            changed = !additions.empty();
            result.reachable_indices.insert(result.reachable_indices.end(), additions.begin(), additions.end());
            ++result.rounds;
        } while (changed);
        finite_inhabitation_cache_[family] = result;
        if (rainfall_)
            rainfall_->record(
                "derive", "proof.inductive.finite-inhabitation", {"proof-inductive:" + family}, "fine.proof-elaborator",
                "Fine computes the exact least constructor closure when every proof index has a small finite value "
                "domain",
                {RainfallRecorder::string_field("family", family),
                 RainfallRecorder::number_field("domain_states", domain.size()),
                 RainfallRecorder::number_field("reachable_states", result.reachable_indices.size()),
                 RainfallRecorder::number_field("rounds", result.rounds),
                 RainfallRecorder::number_field("solver_checks", result.solver_checks),
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

    std::optional<bool> ProofEngine::ground_least_inhabited(InductiveType const &type) {
        std::function<bool(z3::expr const &)> is_ground = [&](z3::expr const &expression) {
            if (!expression.is_app())
                return false;
            if (expression.num_args() == 0 && expression.decl().decl_kind() == Z3_OP_UNINTERPRETED)
                return false;
            for (unsigned i = 0; i < expression.num_args(); ++i)
                if (!is_ground(expression.arg(i)))
                    return false;
            return true;
        };
        std::function<bool(z3::expr const &)> contains_source_function = [&](z3::expr const &expression) {
            if (!expression.is_app())
                return true;
            Z3_decl_kind kind = expression.decl().decl_kind();
            if (kind == Z3_OP_RECURSIVE || (kind == Z3_OP_UNINTERPRETED && expression.num_args() != 0))
                return true;
            for (unsigned i = 0; i < expression.num_args(); ++i)
                if (contains_source_function(expression.arg(i)))
                    return true;
            return false;
        };
        if (!std::all_of(type.indices.begin(), type.indices.end(),
                         [&](ValueTerm const &index) { return is_ground(index.expression); }))
            return std::nullopt;

        auto cached = std::find_if(
            ground_inhabitation_cache_.begin(), ground_inhabitation_cache_.end(), [&](GroundInhabitation const &entry) {
                return entry.family == type.family && entry.indices.size() == type.indices.size() &&
                       std::equal(entry.indices.begin(), entry.indices.end(), type.indices.begin(),
                                  [&](z3::expr const &left, ValueTerm const &right) {
                                      return same_ast(values_.context(), left, right.expression);
                                  });
            });
        if (cached != ground_inhabitation_cache_.end())
            return cached->inhabited;
        std::size_t cache_index = ground_inhabitation_cache_.size();
        std::vector<z3::expr> cached_indices;
        cached_indices.reserve(type.indices.size());
        for (auto const &index : type.indices)
            cached_indices.push_back(index.expression);
        ground_inhabitation_cache_.push_back({type.family, std::move(cached_indices), std::nullopt});

        auto family_found = proof_inductives_.find(type.family);
        if (family_found == proof_inductives_.end())
            throw std::logic_error("proof evidence names an undeclared family");
        syntax::ProofInductiveDecl const &declaration = *family_found->second;
        z3::sort_vector domain(values_.context());
        for (auto const &index : declaration.indices)
            domain.push_back(values_.sort(kind_of(index.type)));
        std::string relation_name = "fine.proof-ground-least." + type.family + "." + std::to_string(cache_index);
        z3::func_decl relation =
            values_.context().function(relation_name.c_str(), domain, values_.context().bool_sort());
        z3::fixedpoint fixedpoint(values_.context());
        z3::params options(values_.context());
        options.set("engine", "spacer");
#ifndef __EMSCRIPTEN__
        options.set("timeout", 1000u);
#endif
        options.set("rlimit", 1000000u);
        fixedpoint.set(options);
        fixedpoint.register_relation(relation);

        std::vector<z3::expr> rules;
        std::size_t recursive_premises = 0;
        try {
            for (std::size_t constructor_index = 0; constructor_index < declaration.constructors.size();
                 ++constructor_index) {
                auto const &constructor = declaration.constructors[constructor_index];
                ValueEnvironment constructor_values;
                z3::expr_vector variables(values_.context());
                for (auto const &parameter : constructor.parameters) {
                    ValueKind kind = kind_of(parameter.type);
                    std::string symbol =
                        "fine.proof-ground-least." + type.family + "." + constructor.name + "." + parameter.name;
                    z3::expr variable = values_.context().constant(symbol.c_str(), values_.sort(kind));
                    variables.push_back(variable);
                    constructor_values.emplace(parameter.name, ValueTerm(kind, std::move(variable)));
                }
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                SemanticProofType semantic_result = elaborate_proof_type(constructor.result_type, constructor_values,
                                                                         no_proofs, no_proof_order, no_absorbed);
                auto result = std::get_if<InductiveType>(&semantic_result);
                if (!result || result->family != type.family || result->indices.size() != type.indices.size())
                    throw std::logic_error("checked proof constructor changed family or arity");
                if (std::any_of(result->indices.begin(), result->indices.end(),
                                [&](ValueTerm const &index) { return contains_source_function(index.expression); }))
                    return std::nullopt;

                z3::expr body = values_.context().bool_val(true);
                for (auto const &constraint : constructor_identity_constraints(constructor, constructor_values)) {
                    if (contains_source_function(constraint))
                        return std::nullopt;
                    body = body && constraint;
                }
                bool supported = true;
                auto add_premise = [&](syntax::CoeffectParameter const &parameter) {
                    if (!supported || parameter.type.kind != syntax::ProofType::Kind::inductive)
                        return;
                    SemanticProofType semantic_premise = elaborate_proof_type(parameter.type, constructor_values,
                                                                              no_proofs, no_proof_order, no_absorbed);
                    auto premise = std::get_if<InductiveType>(&semantic_premise);
                    if (!premise || premise->family != type.family) {
                        supported = false;
                        return;
                    }
                    if (std::any_of(premise->indices.begin(), premise->indices.end(), [&](ValueTerm const &index) {
                            return contains_source_function(index.expression);
                        })) {
                        supported = false;
                        return;
                    }
                    z3::expr_vector arguments(values_.context());
                    for (auto const &index : premise->indices)
                        arguments.push_back(index.expression);
                    body = body && relation(arguments);
                    ++recursive_premises;
                };
                for (auto const &parameter : constructor.explicit_proof_parameters)
                    add_premise(parameter);
                for (auto const &parameter : constructor.proof_parameters)
                    add_premise(parameter);
                if (!supported)
                    return std::nullopt;

                z3::expr_vector result_arguments(values_.context());
                for (auto const &index : result->indices)
                    result_arguments.push_back(index.expression);
                z3::expr rule = z3::implies(body, relation(result_arguments));
                if (!variables.empty())
                    rule = z3::forall(variables, rule);
                rules.push_back(rule);
                std::string rule_name =
                    "fine.proof-ground-least.rule." + type.family + "." + std::to_string(constructor_index);
                fixedpoint.add_rule(rules.back(), values_.context().str_symbol(rule_name.c_str()));
            }

            z3::expr_vector query_arguments(values_.context());
            for (auto const &index : type.indices)
                query_arguments.push_back(index.expression);
            z3::expr target = relation(query_arguments);
            std::string query_relation_name =
                "fine.proof-ground-least.query." + type.family + "." + std::to_string(cache_index);
            z3::func_decl query_relation =
                values_.context().function(query_relation_name.c_str(), 0, nullptr, values_.context().bool_sort());
            fixedpoint.register_relation(query_relation);
            z3::expr query_rule = z3::implies(target, query_relation());
            fixedpoint.add_rule(query_rule, values_.context().str_symbol("fine.proof-ground-least.query-rule"));
            z3::expr query = query_relation();
            z3::check_result status = fixedpoint.query(query);
            std::optional<bool> result;
            if (status == z3::sat)
                result = true;
            else if (status == z3::unsat)
                result = false;
            ground_inhabitation_cache_[cache_index].inhabited = result;
            if (rainfall_) {
                std::vector<std::string> rule_terms;
                rule_terms.reserve(rules.size());
                for (auto const &rule : rules)
                    rule_terms.push_back(rainfall_->term(rule, "proof-ground-least-rule"));
                std::string target_term = rainfall_->term(target, "proof-ground-least-target");
                std::string query_rule_term = rainfall_->term(query_rule, "proof-ground-least-query-rule");
                rainfall_->record(
                    "observe", "proof.inductive.ground-inhabitation", {"proof-inductive:" + type.family},
                    "fine.proof-elaborator",
                    "A restricted Horn relation checks one closed index against the source family's least "
                    "constructor closure",
                    {RainfallRecorder::string_field("family", type.family),
                     RainfallRecorder::string_field("target", target_term),
                     RainfallRecorder::string_field("query_rule", query_rule_term),
                     RainfallRecorder::raw_field("rules", RainfallRecorder::string_array(rule_terms)),
                     RainfallRecorder::number_field("constructor_rules", rules.size()),
                     RainfallRecorder::number_field("recursive_premises", recursive_premises),
                     RainfallRecorder::string_field("status", status == z3::sat     ? "sat"
                                                              : status == z3::unsat ? "unsat"
                                                                                    : "unknown"),
                     RainfallRecorder::boolean_field("ground_indices", true),
                     RainfallRecorder::boolean_field("same_family_premises_only", true),
                     RainfallRecorder::number_field("timeout_ms",
#ifdef __EMSCRIPTEN__
                                                    0
#else
                                                    1000
#endif
                                                    ),
                     RainfallRecorder::number_field("resource_limit", 1000000)});
            }
            return result;
        } catch (z3::exception const &) {
            return std::nullopt;
        }
    }

    ProofEngine::IndexedPremiseShape
    ProofEngine::constructor_indexed_premise_shape(syntax::ProofConstructorDecl const &constructor,
                                                   ValueEnvironment const &constructor_values,
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
            SemanticProofType premise =
                elaborate_proof_type(parameter.type, constructor_values, no_proofs, no_proof_order, no_absorbed);
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
            shape.covers.push_back(inductive_head_cover(*inductive, evidence_name + "." + parameter.name, expanding));
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
        if (auto inhabited = ground_least_inhabited(type); inhabited && !*inhabited)
            return values_.context().bool_val(false);
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
