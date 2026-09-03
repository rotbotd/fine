#include "elaboration_internal.h"

// Indexed proof evidence: constructor formation, typed holes, proof matching,
// structural induction, context absorption, and proof-level declarations.
namespace fine::elaboration {

    std::vector<ProofCandidate>
    ProofEngine::enumerate_inductive_proof_candidates(syntax::ProofType const &expected_syntax,
                                                      InductiveType const &expected, ProofEnvironment const &proofs,
                                                      std::vector<std::string> const &proof_order) {
        std::vector<ProofCandidate> candidates;
        for (auto const &candidate_name : proof_order) {
            auto found = proofs.find(candidate_name);
            if (found == proofs.end())
                continue;
            auto inductive = std::get_if<InductiveType>(&found->second.type);
            if (inductive && same_type(values_.context(), *inductive, expected))
                candidates.push_back(
                    {candidate_name, "exact-local", candidate_name, std::nullopt, {}, {}, 1, std::nullopt, {}});
        }

        if (!active_inductive_function_ || !active_inductive_function_->induction_parameter)
            return candidates;
        syntax::ProofFunctionDecl const &function = *active_inductive_function_;
        auto instantiation = infer_inductive_value_arguments(function, expected_syntax, expected);
        if (!instantiation)
            return candidates;

        std::vector<std::vector<std::string>> argument_frontiers;
        for (auto const &parameter : function.proof_parameters) {
            ProofEnvironment no_proofs;
            std::vector<std::string> no_proof_order;
            std::vector<z3::expr> no_absorbed;
            SemanticProofType parameter_type =
                elaborate_proof_type(parameter.type, instantiation->values, no_proofs, no_proof_order, no_absorbed);
            std::vector<std::string> frontier;
            for (auto const &proof_name : proof_order) {
                auto found = proofs.find(proof_name);
                if (found == proofs.end() || !same_type(values_.context(), found->second.type, parameter_type))
                    continue;
                if (parameter.name == *function.induction_parameter &&
                    (!found->second.structural_root || *found->second.structural_root != *function.induction_parameter))
                    continue;
                frontier.push_back(proof_name);
            }
            if (frontier.empty())
                return candidates;
            argument_frontiers.push_back(std::move(frontier));
        }

        std::vector<std::vector<std::string>> combinations(1);
        for (auto const &frontier : argument_frontiers) {
            std::vector<std::vector<std::string>> next;
            for (auto const &combination : combinations)
                for (auto const &argument : frontier) {
                    auto extended = combination;
                    extended.push_back(argument);
                    next.push_back(std::move(extended));
                }
            combinations = std::move(next);
        }
        for (auto const &arguments : combinations) {
            std::size_t cost = 1 + arguments.size();
            if (cost > max_proof_search_cost)
                continue;
            std::ostringstream source;
            source << function.name << '(';
            std::vector<std::string> index_arguments;
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                if (i)
                    source << ", ";
                std::string const &argument = instantiation->sources.at(function.parameters[i].name);
                source << argument;
                index_arguments.push_back(argument);
            }
            source << ')';
            if (!arguments.empty()) {
                source << " using [";
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (i)
                        source << ", ";
                    source << function.proof_parameters[i].name << " = " << arguments[i];
                }
                source << ']';
            }
            candidates.push_back({source.str(),
                                  "induction-hypothesis",
                                  std::nullopt,
                                  function.name,
                                  std::move(index_arguments),
                                  arguments,
                                  cost,
                                  std::nullopt,
                                  {}});
        }
        return candidates;
    }
    ProofEvidence ProofEngine::elaborate_inductive_hole(syntax::ProofExpr const &expression,
                                                        syntax::ProofType const &expected_syntax,
                                                        InductiveType expected, ProofEnvironment const &proofs,
                                                        std::vector<std::string> const &proof_order, std::string name,
                                                        std::string const &run) {
        if (options_.require_materialized_proofs)
            reject(expression.span, "proof hole remains after materialization");
        if (options_.proof_selector == ProofSelector::z3_model)
            reject(expression.span,
                   "Z3 proof selector does not yet cover indexed proof holes; use deterministic selection");

        std::vector<ProofCandidate> candidates =
            enumerate_inductive_proof_candidates(expected_syntax, expected, proofs, proof_order);
        std::string proof_source;
        std::string type_source;
        std::string hole;
        std::vector<std::string> candidate_events;
        if (rainfall_) {
            proof_source = rainfall_->source_node(expression.node_id, expression.span, "proof.expression.hole");
            type_source = rainfall_->source_node(expected_syntax.node_id, expected_syntax.span, "proof-type.inductive");
            hole = "proof-hole:" + proof_source;
            rainfall_->record(
                "object", "proof.search.open", {"run:" + run, hole}, "fine.typed-proof-search",
                "An indexed proof hole opens with exact local evidence and structurally admitted induction "
                "hypothesis applications only",
                {RainfallRecorder::string_field("id", hole), RainfallRecorder::string_field("source", proof_source),
                 RainfallRecorder::string_field("type_source", type_source),
                 RainfallRecorder::string_field("binding", name),
                 RainfallRecorder::string_field("expected_type", print_proof_type(expected_syntax)),
                 RainfallRecorder::string_field("proposition", ""),
                 RainfallRecorder::raw_field("grammar", "[\"exact-local\",\"induction-hypothesis\"]"),
                 RainfallRecorder::number_field("max_cost", max_proof_search_cost),
                 RainfallRecorder::boolean_field("ill_typed_candidates_enumerated", false),
                 RainfallRecorder::boolean_field("nondecreasing_ih_candidates_enumerated", false)});
            for (auto const &candidate : candidates) {
                std::vector<RainfallField> data = {
                    RainfallRecorder::string_field("hole", hole),
                    RainfallRecorder::string_field("body", candidate.source),
                    RainfallRecorder::string_field("production", candidate.production),
                    RainfallRecorder::string_field("expected_type", print_proof_type(expected_syntax)),
                    RainfallRecorder::boolean_field("exact_type", true),
                    RainfallRecorder::boolean_field("runtime_value_created", false),
                    RainfallRecorder::boolean_field("complete", true),
                    RainfallRecorder::number_field("closed_frontier", 1),
                    RainfallRecorder::number_field("open_leaves", 0),
                    RainfallRecorder::number_field("cost", candidate.cost),
                };
                if (candidate.local_proof)
                    data.push_back(RainfallRecorder::string_field("proof", *candidate.local_proof));
                if (candidate.proof_function) {
                    auto const &function = *active_inductive_function_;
                    auto parameter = std::find_if(function.proof_parameters.begin(), function.proof_parameters.end(),
                                                  [&](syntax::CoeffectParameter const &item) {
                                                      return item.name == *function.induction_parameter;
                                                  });
                    std::size_t position = static_cast<std::size_t>(parameter - function.proof_parameters.begin());
                    std::string const &recursive = candidate.proof_arguments[position];
                    auto const &evidence = proofs.at(recursive);
                    data.push_back(RainfallRecorder::string_field("function", *candidate.proof_function));
                    data.push_back(RainfallRecorder::raw_field(
                        "index_arguments", RainfallRecorder::string_array(candidate.index_arguments)));
                    data.push_back(RainfallRecorder::raw_field(
                        "proof_arguments", RainfallRecorder::string_array(candidate.proof_arguments)));
                    data.push_back(
                        RainfallRecorder::string_field("induction_parameter", *function.induction_parameter));
                    data.push_back(
                        RainfallRecorder::string_field("parent_evidence", evidence.structural_parent.value_or("")));
                    data.push_back(RainfallRecorder::string_field("recursive_evidence", recursive));
                }
                candidate_events.push_back(rainfall_->record(
                    "derive", "proof.search.candidate", {"run:" + run, hole}, "fine.typed-proof-search",
                    "An exact indexed proof candidate entered the frontier after structural descent "
                    "filtering",
                    data));
            }
        }

        if (candidates.empty())
            reject(expression.span, "indexed proof hole `" + name +
                                        "` has no candidate in grammar "
                                        "[exact-local, induction-hypothesis]");
        ProofCandidate const &selected = candidates.front();
        materializations_.request_materialization(syntax::ConcreteRange::from_span(expression.span), selected.source,
                                                  expression.span);
        ++holes_filled_;
        if (rainfall_) {
            std::string selection = rainfall_->record(
                "transition", "proof.search.select", {"run:" + run, hole}, "fine.typed-proof-search",
                "The first deterministic exact indexed proof candidate is selected for materialization",
                {RainfallRecorder::string_field("hole", hole),
                 RainfallRecorder::string_field("candidate", candidate_events.front()),
                 RainfallRecorder::string_field("body", selected.source),
                 RainfallRecorder::string_field("production", selected.production),
                 RainfallRecorder::boolean_field("complete", true)});
            std::vector<std::string> residual(candidate_events.begin() + 1, candidate_events.end());
            rainfall_->record(
                "transition", "proof.search.close", {"run:" + run, hole}, "fine.typed-proof-search",
                "The indexed hole has a checked source witness and a complete residual frontier",
                {RainfallRecorder::string_field("hole", hole), RainfallRecorder::string_field("selection", selection),
                 RainfallRecorder::string_field("selected_candidate", candidate_events.front()),
                 RainfallRecorder::raw_field("residual_candidates", RainfallRecorder::string_array(residual)),
                 RainfallRecorder::string_field("status", "selected"),
                 RainfallRecorder::boolean_field("materialization_requested", true)});
        }
        output_ << "filled proof hole: " << name << " <- " << selected.source << " (typed search)\n";
        std::string formation = selected.local_proof ? "search:exact-local:" + *selected.local_proof
                                                     : "search:induction-hypothesis:" + *selected.proof_function;
        return {std::move(name),          std::move(expected), std::move(formation), expression.span, "", "",
                expected_syntax.arguments};
    }
    ProofEvidence ProofEngine::elaborate_inductive_proof(
        syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax, InductiveType expected,
        ValueEnvironment const &values, ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
        std::vector<z3::expr> const &absorbed, std::string name, std::string const &run) {
        if (expression.kind == syntax::ProofExpr::Kind::name) {
            auto found = proofs.find(expression.name);
            if (found == proofs.end())
                reject(expression.span, "unknown proof `" + expression.name + "`");
            auto inductive = std::get_if<InductiveType>(&found->second.type);
            if (!inductive || !same_type(values_.context(), *inductive, expected))
                reject(expression.span, "proof `" + expression.name + "` has the wrong inductive type");
            return {std::move(name),           std::move(expected), "alias:" + expression.name, expression.span, "", "",
                    found->second.index_syntax};
        }
        if (expression.kind == syntax::ProofExpr::Kind::reflexivity)
            reject(expression.span, "`refl` constructs identity evidence, not `" + expected.family + "`");
        if (expression.kind == syntax::ProofExpr::Kind::hole)
            return elaborate_inductive_hole(expression, expected_syntax, std::move(expected), proofs, proof_order,
                                            std::move(name), run);

        auto found = proof_constructors_.find(expression.name);
        if (found == proof_constructors_.end()) {
            if (proof_functions_.contains(expression.name))
                return elaborate_proof_application(expression, expected_syntax, SemanticProofType(std::move(expected)),
                                                   values, proofs, proof_order, absorbed, std::move(name), run);
            reject(expression.span, "unknown proof constructor `" + expression.name + "`");
        }
        auto const &constructor = *found->second;
        std::size_t positional_count = constructor.parameters.size() + constructor.explicit_proof_parameters.size();
        if (expression.argument_kinds.size() != positional_count)
            reject(expression.span, "proof constructor `" + expression.name + "` expects " +
                                        std::to_string(positional_count) + " positional arguments");

        ValueEnvironment indices;
        std::vector<std::string> value_sources;
        for (std::size_t i = 0; i < constructor.parameters.size(); ++i) {
            syntax::ValueExpr argument_syntax = positional_value_argument(expression, i);
            ValueTerm argument = values_.elaborate_value(argument_syntax, values, proofs, proof_order, absorbed);
            values_.require_known_type(constructor.parameters[i].type);
            if (argument.kind != kind_of(constructor.parameters[i].type))
                reject(argument_syntax.span,
                       "value argument `" + constructor.parameters[i].name + "` has the wrong value type");
            indices.emplace(constructor.parameters[i].name, std::move(argument));
            value_sources.push_back(print_value(argument_syntax));
        }

        ProofEnvironment constructor_proofs;
        std::vector<std::string> constructor_proof_order;
        std::vector<z3::expr> no_absorbed;
        std::vector<std::string> proof_sources;
        for (std::size_t i = 0; i < constructor.explicit_proof_parameters.size(); ++i) {
            auto const &parameter = constructor.explicit_proof_parameters[i];
            auto const &argument_expression = positional_proof_argument(expression, constructor.parameters.size() + i);
            SemanticProofType parameter_type =
                elaborate_proof_type(parameter.type, indices, constructor_proofs, constructor_proof_order, no_absorbed);
            ProofEvidence argument =
                elaborate_any_proof(argument_expression, parameter.type, std::move(parameter_type), values, proofs,
                                    proof_order, absorbed, name + "." + parameter.name, run, {}, {});
            if (!argument.complete)
                reject(argument_expression.span,
                       "partial checkpoints inside indexed proof constructors are not yet supported");
            proof_sources.push_back(print_proof(argument_expression));
            constructor_proofs.emplace(parameter.name, std::move(argument));
            constructor_proof_order.push_back(parameter.name);
        }

        std::map<std::string, std::size_t> explicit_coeffects;
        for (std::size_t i = 0; i < expression.using_coeffects.size(); ++i) {
            if (!explicit_coeffects.emplace(expression.using_coeffects[i], i).second)
                reject(expression.using_spans[i],
                       "duplicate explicit coeffect `" + expression.using_coeffects[i] + "`");
        }
        std::vector<std::pair<std::string, std::string>> chosen_locals;
        std::vector<std::string> coeffect_sources;
        for (auto const &parameter : constructor.proof_parameters) {
            SemanticProofType parameter_type =
                elaborate_proof_type(parameter.type, indices, constructor_proofs, constructor_proof_order, no_absorbed);
            std::string source;
            ProofEvidence supplied(parameter.name, parameter_type, "constructor-coeffect", parameter.span, "", "");
            bool explicit_choice = false;
            auto explicit_found = explicit_coeffects.find(parameter.name);
            if (explicit_found != explicit_coeffects.end()) {
                auto const &argument_expression = expression.using_proofs[explicit_found->second];
                supplied = elaborate_any_proof(argument_expression, parameter.type, std::move(parameter_type), values,
                                               proofs, proof_order, absorbed, name + "." + parameter.name, run, {}, {});
                source = print_proof(argument_expression);
                explicit_choice = true;
                explicit_coeffects.erase(explicit_found);
            }
            else {
                if (options_.require_explicit_coeffects)
                    reject(expression.span, "implicit coeffect `" + expression.name + "." + parameter.name +
                                                "` remains after materialization");
                for (auto const &candidate : proof_order) {
                    auto candidate_found = proofs.find(candidate);
                    if (candidate_found != proofs.end() &&
                        same_type(values_.context(), candidate_found->second.type, parameter_type)) {
                        source = candidate;
                        supplied = candidate_found->second;
                        supplied.name = parameter.name;
                        break;
                    }
                }
                if (source.empty())
                    reject(expression.span, "missing caller proof for coeffect `" + expression.name + "." +
                                                parameter.name + " : " + print_proof_type(parameter.type) + "`");
                chosen_locals.emplace_back(parameter.name, source);
            }
            if (!supplied.complete)
                reject(expression.span, "open proof cannot satisfy constructor coeffect `" + parameter.name + "`");
            coeffect_sources.push_back(source);
            constructor_proofs.insert_or_assign(parameter.name, std::move(supplied));
            constructor_proof_order.push_back(parameter.name);
            ++coeffects_resolved_;
            output_ << "resolved coeffect: " << expression.name << '.' << parameter.name << " <- " << source
                    << (explicit_choice ? " (explicit)" : " (lexical search)") << '\n';
            if (rainfall_) {
                rainfall_->record("derive", "coeffect.demand.instantiate", {"proof-constructor:" + expression.name},
                                  "fine.proof-elaborator",
                                  "Value and explicit proof arguments instantiate a proof constructor's virtual demand",
                                  {RainfallRecorder::string_field("constructor", expression.name),
                                   RainfallRecorder::string_field("coeffect", parameter.name),
                                   RainfallRecorder::string_field("proof_type", print_proof_type(parameter.type)),
                                   RainfallRecorder::boolean_field("proof_constructor", true),
                                   RainfallRecorder::boolean_field("proof_identity_observable", false)});
                rainfall_->record(
                    "derive", "coeffect.resolve", {"proof-constructor:" + expression.name}, "fine.lexical-proof-search",
                    "A proof constructor demand is satisfied without making the selected proof observable",
                    {RainfallRecorder::string_field("constructor", expression.name),
                     RainfallRecorder::string_field("coeffect", parameter.name),
                     RainfallRecorder::string_field("proof", source),
                     RainfallRecorder::string_field("mode", explicit_choice ? "explicit" : "exact-local"),
                     RainfallRecorder::boolean_field("proof_constructor", true),
                     RainfallRecorder::boolean_field("proof_identity_observable", false)});
                rainfall_->record("derive", "coeffect.use", {"proof-constructor:" + expression.name},
                                  "fine.proof-context",
                                  "Resolved evidence satisfies the constructor demand without becoming a proof child",
                                  {RainfallRecorder::string_field("constructor", expression.name),
                                   RainfallRecorder::string_field("coeffect", parameter.name),
                                   RainfallRecorder::string_field("proof", source),
                                   RainfallRecorder::boolean_field("runtime_argument_created", false),
                                   RainfallRecorder::boolean_field("proof_constructor", true),
                                   RainfallRecorder::boolean_field("proof_identity_observable", false)});
            }
        }
        if (!explicit_coeffects.empty())
            reject(expression.span,
                   "proof constructor call supplies unknown coeffect `" + explicit_coeffects.begin()->first + "`");
        if (expression.using_coeffects.empty() && !chosen_locals.empty()) {
            std::ostringstream insertion;
            insertion << " using [";
            for (std::size_t i = 0; i < chosen_locals.size(); ++i) {
                if (i)
                    insertion << ", ";
                insertion << chosen_locals[i].first << " = " << chosen_locals[i].second;
            }
            insertion << ']';
            materializations_.request_materialization(syntax::ConcreteRange::empty_at(expression.call_argument_end),
                                                      insertion.str(), expression.span);
        }
        SemanticProofType result_type = elaborate_proof_type(constructor.result_type, indices, constructor_proofs,
                                                             constructor_proof_order, no_absorbed);
        auto inductive_result = std::get_if<InductiveType>(&result_type);
        if (!inductive_result || !same_type(values_.context(), *inductive_result, expected))
            reject(expression.span,
                   "proof constructor application `" + print_proof(expression) + "` has the wrong result type");

        if (rainfall_)
            rainfall_->record(
                "derive", "proof.inductive.constructor.apply", {"run:" + run}, "fine.proof-elaborator",
                "A static indexed constructor forms proof evidence without creating a runtime datatype value",
                {RainfallRecorder::string_field("family", expected.family),
                 RainfallRecorder::string_field("constructor", constructor.name),
                 RainfallRecorder::string_field("body", print_proof(expression)),
                 RainfallRecorder::raw_field("value_arguments", RainfallRecorder::string_array(value_sources)),
                 RainfallRecorder::raw_field("proof_arguments", RainfallRecorder::string_array(proof_sources)),
                 RainfallRecorder::raw_field("coeffects", RainfallRecorder::string_array(coeffect_sources)),
                 RainfallRecorder::boolean_field("runtime_value_created", false)});
        return {
            std::move(name),          std::move(expected), "constructor:" + constructor.name, expression.span, "", "",
            expected_syntax.arguments};
    }
    ProofEvidence ProofEngine::elaborate_proof_match(
        syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax, SemanticProofType expected,
        ValueEnvironment const &values, ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
        std::vector<z3::expr> const &absorbed, std::string name, std::string const &run) {
        auto scrutinee_found = proofs.find(expression.matched_proof);
        if (scrutinee_found == proofs.end())
            reject(expression.span, "unknown proof `" + expression.matched_proof + "`");
        auto scrutinee = std::get_if<InductiveType>(&scrutinee_found->second.type);
        if (!scrutinee)
            reject(expression.span,
                   "proof match scrutinee `" + expression.matched_proof + "` is not indexed-family evidence");
        auto family_found = proof_inductives_.find(scrutinee->family);
        if (family_found == proof_inductives_.end())
            throw std::logic_error("proof evidence names an undeclared family");
        syntax::ProofInductiveDecl const &family = *family_found->second;
        std::string match_scope = "proof-match:" + std::to_string(expression.node_id);

        struct ReachableArm {
            syntax::ProofConstructorDecl const *constructor;
            ValueEnvironment constructor_values;
            ValueEnvironment refined_values;
            std::vector<std::string> refined_indices;
        };
        std::map<std::string, ReachableArm> reachable;
        for (auto const &constructor : family.constructors) {
            std::map<std::string, ValueKind> parameter_kinds;
            for (auto const &parameter : constructor.parameters)
                parameter_kinds.emplace(parameter.name, kind_of(parameter.type));
            ValueEnvironment constructor_values;
            bool possible = true;
            for (std::size_t i = 0; i < scrutinee->indices.size(); ++i) {
                bool refinable = i < scrutinee_found->second.index_syntax.size() &&
                                 is_refinable_index(scrutinee_found->second.index_syntax[i], scrutinee->indices[i]);
                if (!refinable && !values_.match_constructor_index(constructor.result_type.arguments[i],
                                                                   scrutinee->indices[i].expression, parameter_kinds,
                                                                   constructor_values)) {
                    possible = false;
                    break;
                }
            }
            if (!possible)
                continue;
            for (auto const &parameter : constructor.parameters) {
                if (constructor_values.contains(parameter.name))
                    continue;
                ValueKind kind = kind_of(parameter.type);
                std::string symbol = "fine.proof-match." + std::to_string(expression.node_id) + "." + constructor.name +
                                     "." + parameter.name;
                constructor_values.emplace(
                    parameter.name, ValueTerm(kind, values_.context().constant(symbol.c_str(), values_.sort(kind))));
            }
            ProofEnvironment no_proofs;
            std::vector<std::string> no_proof_order;
            std::vector<z3::expr> no_absorbed;
            SemanticProofType constructor_result = elaborate_proof_type(constructor.result_type, constructor_values,
                                                                        no_proofs, no_proof_order, no_absorbed);
            auto result_indices = std::get_if<InductiveType>(&constructor_result);
            if (!result_indices || result_indices->family != family.name)
                throw std::logic_error("checked proof constructor changed family");

            ValueEnvironment refined_values = values;
            ValueEnvironment index_refinements;
            for (std::size_t i = 0; i < scrutinee->indices.size(); ++i) {
                bool refinable = i < scrutinee_found->second.index_syntax.size() &&
                                 is_refinable_index(scrutinee_found->second.index_syntax[i], scrutinee->indices[i]);
                if (!refinable) {
                    if (!same_ast(values_.context(), result_indices->indices[i].expression,
                                  scrutinee->indices[i].expression)) {
                        possible = false;
                        break;
                    }
                    continue;
                }
                std::string const &index_name = scrutinee_found->second.index_syntax[i].name;
                auto previous = index_refinements.find(index_name);
                if (previous != index_refinements.end() &&
                    !same_ast(values_.context(), previous->second.expression, result_indices->indices[i].expression)) {
                    possible = false;
                    break;
                }
                if (previous == index_refinements.end())
                    index_refinements.emplace(index_name, result_indices->indices[i]);
            }
            for (auto const &[index_name, refinement] : index_refinements)
                refined_values.insert_or_assign(index_name, refinement);
            std::vector<std::string> refined_indices;
            for (auto const &[index_name, refinement] : index_refinements)
                refined_indices.push_back(index_name);
            if (possible)
                reachable.emplace(constructor.name,
                                  ReachableArm{&constructor, std::move(constructor_values), std::move(refined_values),
                                               std::move(refined_indices)});
        }

        std::set<std::string> seen;
        for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
            std::string const &constructor_name = expression.match_constructors[i];
            auto global = proof_constructors_.find(constructor_name);
            if (global == proof_constructors_.end() || global->second->result_type.name != family.name)
                reject(expression.match_arm_spans[i],
                       "proof constructor `" + constructor_name + "` does not belong to `" + family.name + "`");
            if (!seen.insert(constructor_name).second)
                reject(expression.match_arm_spans[i], "duplicate proof match arm for `" + constructor_name + "`");
            auto arm_found = reachable.find(constructor_name);
            if (arm_found == reachable.end())
                reject(expression.match_arm_spans[i],
                       "unreachable proof match arm `" + constructor_name + "` must be omitted");
            auto const &constructor = *arm_found->second.constructor;
            std::size_t positional_binders =
                constructor.parameters.size() + constructor.explicit_proof_parameters.size();
            if (expression.match_binders[i].size() != positional_binders)
                reject(expression.match_arm_spans[i], "proof match arm `" + constructor_name + "` expects " +
                                                          std::to_string(positional_binders) + " positional binders");

            ValueEnvironment branch_values = arm_found->second.refined_values;
            ProofEnvironment branch_proofs = proofs;
            std::vector<std::string> branch_proof_order = proof_order;
            std::vector<z3::expr> branch_absorbed = absorbed;
            std::set<std::string> arm_names;
            for (std::size_t j = 0; j < constructor.parameters.size(); ++j) {
                std::string const &binder = expression.match_binders[i][j];
                if (!arm_names.insert(binder).second || branch_values.contains(binder) ||
                    branch_proofs.contains(binder))
                    reject(expression.match_arm_spans[i], "duplicate proof match binder `" + binder + "`");
                branch_values.emplace(binder, arm_found->second.constructor_values.at(constructor.parameters[j].name));
            }

            ProofEnvironment constructor_proofs;
            std::vector<std::string> constructor_proof_order;
            auto bind_branch_proof = [&](syntax::CoeffectParameter const &parameter, std::string const &binder,
                                         std::string formation, std::string_view role) {
                SemanticProofType field_type =
                    elaborate_proof_type(parameter.type, arm_found->second.constructor_values, constructor_proofs,
                                         constructor_proof_order, branch_absorbed);
                if (!arm_names.insert(binder).second || branch_values.contains(binder) ||
                    branch_proofs.contains(binder))
                    reject(expression.match_arm_spans[i], "duplicate proof match binder `" + binder + "`");
                ProofEvidence field(binder, std::move(field_type), std::move(formation), expression.match_arm_spans[i],
                                    "", "", parameter.type.arguments);
                auto field_inductive = std::get_if<InductiveType>(&field.type);
                if (active_inductive_function_ && active_inductive_function_->induction_parameter && field_inductive &&
                    field_inductive->family == scrutinee->family) {
                    std::string const &root = *active_inductive_function_->induction_parameter;
                    if (expression.matched_proof == root ||
                        (scrutinee_found->second.structural_root && *scrutinee_found->second.structural_root == root)) {
                        field.structural_root = root;
                        field.structural_parent = expression.matched_proof;
                    }
                }
                auto [inserted, ok] = branch_proofs.emplace(binder, std::move(field));
                branch_proof_order.push_back(binder);
                absorb(inserted->second, branch_absorbed,
                       {"proof-function:" + run, "proof-match:" + expression.matched_proof}, role);
                constructor_proofs.emplace(parameter.name, inserted->second);
                constructor_proof_order.push_back(parameter.name);
            };
            for (std::size_t j = 0; j < constructor.explicit_proof_parameters.size(); ++j) {
                auto const &parameter = constructor.explicit_proof_parameters[j];
                std::string const &binder = expression.match_binders[i][constructor.parameters.size() + j];
                bind_branch_proof(parameter, binder, "proof-match-explicit-field:" + expression.matched_proof,
                                  "proof-match-explicit-field");
            }
            for (auto const &parameter : constructor.proof_parameters) {
                bind_branch_proof(parameter, parameter.name, "proof-match-coeffect:" + expression.matched_proof,
                                  "proof-match-coeffect");
            }
            SemanticProofType branch_expected = elaborate_proof_type(expected_syntax, branch_values, branch_proofs,
                                                                     branch_proof_order, branch_absorbed);
            ProofEvidence branch = elaborate_any_proof(
                expression.match_bodies[i], expected_syntax, std::move(branch_expected), branch_values, branch_proofs,
                branch_proof_order, branch_absorbed, name + "." + constructor_name, run, {}, {});
            if (!branch.complete)
                reject(expression.match_bodies[i].span,
                       "partial checkpoints inside proof matches are not yet supported");
            if (rainfall_) {
                std::vector<std::string> value_binders(expression.match_binders[i].begin(),
                                                       expression.match_binders[i].begin() +
                                                           constructor.parameters.size());
                std::vector<std::string> proof_binders(expression.match_binders[i].begin() +
                                                           constructor.parameters.size(),
                                                       expression.match_binders[i].end());
                std::vector<std::string> coeffect_binders;
                for (auto const &parameter : constructor.proof_parameters)
                    coeffect_binders.push_back(parameter.name);
                rainfall_->record(
                    "derive", "proof.inductive.match.branch", {"proof-function:" + run, match_scope},
                    "fine.proof-elaborator",
                    "One reachable constructor owns its index refinements and bound static/proof fields",
                    {RainfallRecorder::string_field("constructor", constructor_name),
                     RainfallRecorder::raw_field("refined_indices",
                                                 RainfallRecorder::string_array(arm_found->second.refined_indices)),
                     RainfallRecorder::raw_field("value_binders", RainfallRecorder::string_array(value_binders)),
                     RainfallRecorder::raw_field("proof_binders", RainfallRecorder::string_array(proof_binders)),
                     RainfallRecorder::raw_field("coeffect_binders", RainfallRecorder::string_array(coeffect_binders)),
                     RainfallRecorder::boolean_field("runtime_value_created", false)});
            }
        }
        for (auto const &[constructor_name, arm] : reachable)
            if (!seen.contains(constructor_name))
                reject(expression.span, "non-exhaustive proof match: missing `" + constructor_name + "`");

        if (rainfall_)
            rainfall_->record("derive", "proof.inductive.match", {"proof-function:" + run, match_scope},
                              "fine.proof-elaborator",
                              "Constructor-result unification refines branch indices before proof bodies are checked",
                              {RainfallRecorder::string_field("scrutinee", expression.matched_proof),
                               RainfallRecorder::string_field("family", family.name),
                               RainfallRecorder::number_field("reachable_constructors", reachable.size()),
                               RainfallRecorder::number_field("checked_arms", expression.match_constructors.size()),
                               RainfallRecorder::boolean_field("exhaustiveness_after_refinement", true),
                               RainfallRecorder::boolean_field("runtime_value_created", false)});
        std::vector<syntax::ValueExpr> index_syntax;
        if (expected_syntax.kind == syntax::ProofType::Kind::inductive)
            index_syntax = expected_syntax.arguments;
        return {std::move(name),
                std::move(expected),
                "proof-match:" + expression.matched_proof,
                expression.span,
                expected_syntax.kind == syntax::ProofType::Kind::identity ? print_value(expected_syntax.left) : "",
                expected_syntax.kind == syntax::ProofType::Kind::identity ? print_value(expected_syntax.right) : "",
                std::move(index_syntax)};
    }
    ProofEvidence ProofEngine::elaborate_any_proof(syntax::ProofExpr const &expression,
                                                   syntax::ProofType const &expected_syntax, SemanticProofType expected,
                                                   ValueEnvironment const &values, ProofEnvironment const &proofs,
                                                   std::vector<std::string> const &proof_order,
                                                   std::vector<z3::expr> const &absorbed, std::string name,
                                                   std::string const &run, std::string const &proof_source,
                                                   std::string const &type_source) {
        if (expression.kind == syntax::ProofExpr::Kind::match)
            return elaborate_proof_match(expression, expected_syntax, std::move(expected), values, proofs, proof_order,
                                         absorbed, std::move(name), run);
        if (auto identity = std::get_if<IdentityType>(&expected))
            return elaborate_proof(expression, expected_syntax, std::move(*identity), values, proofs, proof_order,
                                   absorbed, std::move(name), run, proof_source, type_source);
        return elaborate_inductive_proof(expression, expected_syntax, std::move(std::get<InductiveType>(expected)),
                                         values, proofs, proof_order, absorbed, std::move(name), run);
    }
    void ProofEngine::absorb(ProofEvidence const &proof, std::vector<z3::expr> &absorbed,
                             std::vector<std::string> within, std::string_view role,
                             std::optional<std::string> source) {
        if (!proof.complete)
            throw std::logic_error("open proof evidence cannot enter an SMT context");
        auto identity = std::get_if<IdentityType>(&proof.type);
        if (!identity)
            return;
        z3::expr proposition = identity->left == identity->right;
        absorbed.push_back(proposition);
        if (!rainfall_)
            return;
        std::string term = rainfall_->term(proposition, "identity-proposition");
        std::vector<RainfallField> data = {
            RainfallRecorder::string_field("proof", proof.name),
            RainfallRecorder::string_field("proposition", term),
            RainfallRecorder::string_field("role", role),
            RainfallRecorder::boolean_field("runtime_value_created", false),
        };
        if (source)
            data.push_back(RainfallRecorder::string_field("source", *source));
        rainfall_->record("derive", "proof.context.absorb", within, "fine.proof-context",
                          "Identity evidence contributes its proposition to this lexical SMT context without "
                          "becoming a runtime value",
                          data);
    }
    void ProofEngine::declare_proof_inductive(syntax::ProofInductiveDecl const &declaration) {
        if (declaration.name == "Id" || proof_inductives_.contains(declaration.name))
            reject(declaration.span, "duplicate proof type `" + declaration.name + "`");
        std::set<std::string> index_names;
        for (auto const &index : declaration.indices) {
            if (!index_names.insert(index.name).second)
                reject(index.span, "duplicate proof index `" + index.name + "`");
            values_.require_known_type(index.type);
        }

        proof_inductives_.emplace(declaration.name, &declaration);
        std::set<std::string> local_constructors;
        for (auto const &constructor : declaration.constructors) {
            if (!local_constructors.insert(constructor.name).second || proof_constructors_.contains(constructor.name) ||
                proof_functions_.contains(constructor.name))
                reject(constructor.span, "duplicate proof constructor `" + constructor.name + "`");
            ValueEnvironment values;
            ProofEnvironment proofs;
            std::vector<std::string> proof_order;
            std::vector<z3::expr> absorbed;
            std::set<std::string> names;
            for (auto const &parameter : constructor.parameters) {
                if (!names.insert(parameter.name).second)
                    reject(parameter.span, "duplicate constructor parameter `" + parameter.name + "`");
                values_.require_known_type(parameter.type);
                ValueKind kind = kind_of(parameter.type);
                std::string symbol = "fine.proof-constructor." + constructor.name + "." + parameter.name;
                values.emplace(parameter.name,
                               ValueTerm(kind, values_.context().constant(symbol.c_str(), values_.sort(kind))));
            }
            for (auto const &parameter : constructor.explicit_proof_parameters) {
                if (!names.insert(parameter.name).second)
                    reject(parameter.span, "duplicate constructor parameter `" + parameter.name + "`");
                SemanticProofType type = elaborate_proof_type(parameter.type, values, proofs, proof_order, absorbed);
                proofs.emplace(parameter.name,
                               ProofEvidence(parameter.name, std::move(type), "proof-constructor-explicit-parameter",
                                             parameter.span, "", ""));
                proof_order.push_back(parameter.name);
            }
            for (auto const &parameter : constructor.proof_parameters) {
                if (!names.insert(parameter.name).second)
                    reject(parameter.span, "duplicate constructor parameter `" + parameter.name + "`");
                SemanticProofType type = elaborate_proof_type(parameter.type, values, proofs, proof_order, absorbed);
                proofs.emplace(parameter.name, ProofEvidence(parameter.name, std::move(type),
                                                             "proof-constructor-coeffect", parameter.span, "", ""));
                proof_order.push_back(parameter.name);
            }
            SemanticProofType result =
                elaborate_proof_type(constructor.result_type, values, proofs, proof_order, absorbed);
            auto inductive = std::get_if<InductiveType>(&result);
            if (!inductive || inductive->family != declaration.name)
                reject(constructor.result_type.span,
                       "proof constructor `" + constructor.name + "` must return `" + declaration.name + "(...)`");
            proof_constructors_.emplace(constructor.name, &constructor);
        }
        output_ << "declared proof inductive: " << declaration.name << " (" << declaration.constructors.size()
                << " constructors, static)\n";
        if (rainfall_)
            rainfall_->record("object", "proof.inductive.declare", {"proof-inductive:" + declaration.name},
                              "fine.proof-elaborator",
                              "Indexed proof constructors declared only in the static evidence layer",
                              {RainfallRecorder::string_field("family", declaration.name),
                               RainfallRecorder::number_field("indices", declaration.indices.size()),
                               RainfallRecorder::number_field("constructors", declaration.constructors.size()),
                               RainfallRecorder::boolean_field("runtime_datatype_created", false)});
    }
    void ProofEngine::declare_proof_function(syntax::ProofFunctionDecl const &declaration) {
        if (proof_functions_.contains(declaration.name) || values_.has_function(declaration.name) ||
            values_.has_constructor(declaration.name) || proof_constructors_.contains(declaration.name))
            reject(declaration.span, "duplicate function `" + declaration.name + "`");
        ValueEnvironment indices;
        ProofEnvironment proofs;
        std::vector<std::string> proof_order;
        std::vector<z3::expr> absorbed;
        std::set<std::string> names;
        for (auto const &parameter : declaration.parameters) {
            if (!names.insert(parameter.name).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            values_.require_known_type(parameter.type);
            ValueKind kind = kind_of(parameter.type);
            std::string symbol = "fine.proof-function." + declaration.name + "." + parameter.name;
            indices.emplace(parameter.name,
                            ValueTerm(kind, values_.context().constant(symbol.c_str(), values_.sort(kind))));
        }

        std::vector<std::string> parameter_sources;
        for (auto const &parameter : declaration.proof_parameters) {
            if (!names.insert(parameter.name).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            SemanticProofType type = elaborate_proof_type(parameter.type, indices, proofs, proof_order, absorbed);
            std::vector<syntax::ValueExpr> index_syntax;
            std::string left_source;
            std::string right_source;
            if (parameter.type.kind == syntax::ProofType::Kind::identity) {
                left_source = print_value(parameter.type.left);
                right_source = print_value(parameter.type.right);
            }
            else {
                index_syntax = parameter.type.arguments;
            }
            ProofEvidence evidence(parameter.name, std::move(type), "proof-function-parameter", parameter.span,
                                   std::move(left_source), std::move(right_source), std::move(index_syntax));
            if (rainfall_)
                parameter_sources.push_back(rainfall_->source_node(
                    parameter.type.node_id, parameter.type.span,
                    parameter.type.kind == syntax::ProofType::Kind::identity ? "proof-type.identity"
                                                                             : "proof-type.inductive"));
            auto [inserted, ok] = proofs.emplace(parameter.name, std::move(evidence));
            proof_order.push_back(parameter.name);
            absorb(inserted->second, absorbed, {"proof-function:" + declaration.name}, "proof-function-parameter");
        }
        SemanticProofType result_type =
            elaborate_proof_type(declaration.result_type, indices, proofs, proof_order, absorbed);
        std::optional<z3::expr> result_proposition;
        if (declaration.induction_parameter) {
            if (!declaration.has_body)
                reject(declaration.span, "`inducts` requires a body-bearing proof function");
            auto parameter = proofs.find(*declaration.induction_parameter);
            if (parameter == proofs.end())
                reject(declaration.span, "unknown induction parameter `" + *declaration.induction_parameter + "`");
            if (!std::holds_alternative<InductiveType>(parameter->second.type))
                reject(declaration.span,
                       "induction parameter `" + *declaration.induction_parameter + "` is not indexed-family evidence");
            proof_functions_.emplace(declaration.name, &declaration);
            active_inductive_function_ = &declaration;
        }
        if (declaration.has_body) {
            (void)elaborate_any_proof(declaration.body, declaration.result_type, result_type, indices, proofs,
                                      proof_order, absorbed, declaration.name + ".result", declaration.name, {}, {});
            active_inductive_function_ = nullptr;
        }
        else {
            auto identity = std::get_if<IdentityType>(&result_type);
            if (!identity)
                reject(declaration.span,
                       "proof function `" + declaration.name + "` returning inductive evidence requires a body");
            result_proposition.emplace(identity->left == identity->right);
            z3::solver solver(values_.context());
            for (auto const &assumption : absorbed)
                solver.add(assumption);
            solver.add(!*result_proposition);
            if (solver.check() != z3::unsat)
                reject(declaration.span, "proof function `" + declaration.name +
                                             "` does not establish its result "
                                             "from its proof parameters");
        }

        if (!declaration.induction_parameter)
            proof_functions_.emplace(declaration.name, &declaration);
        bool identity_searchable = declaration.result_type.kind == syntax::ProofType::Kind::identity &&
                                   std::all_of(declaration.proof_parameters.begin(), declaration.proof_parameters.end(),
                                               [](syntax::CoeffectParameter const &parameter) {
                                                   return parameter.type.kind == syntax::ProofType::Kind::identity;
                                               });
        if (identity_searchable)
            proof_function_order_.push_back(declaration.name);
        ++functions_verified_;
        output_ << "verified proof function: " << declaration.name << '\n';
        if (rainfall_) {
            std::string result_source = rainfall_->source_node(
                declaration.result_type.node_id, declaration.result_type.span,
                declaration.result_type.kind == syntax::ProofType::Kind::identity ? "proof-type.identity"
                                                                                  : "proof-type.inductive");
            std::string proposition =
                result_proposition ? rainfall_->term(*result_proposition, "proof-function-result-proposition") : "";
            rainfall_->record(
                "transition", "proof.function.verify", {"proof-function:" + declaration.name}, "fine.proof-elaborator",
                declaration.has_body
                    ? "A named proof-level function body constructs its static result under virtual parameters"
                    : "A named proof-level function result is refuted under its absorbed proof parameters",
                {RainfallRecorder::string_field("function", declaration.name),
                 RainfallRecorder::raw_field("parameter_sources", RainfallRecorder::string_array(parameter_sources)),
                 RainfallRecorder::string_field("result_source", result_source),
                 RainfallRecorder::string_field("result_proposition", proposition),
                 RainfallRecorder::string_field("status", declaration.has_body ? "body-checked" : "unsat"),
                 RainfallRecorder::boolean_field("runtime_function_created", false)});
        }
    }

}  // namespace fine::elaboration
