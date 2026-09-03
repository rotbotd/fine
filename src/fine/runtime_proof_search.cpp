#include "runtime_internal.h"

// Bounded proof search: surface argument classification, named proof-function
// application, deterministic identity grammars, and Z3 model selection.
namespace fine::runtime_detail {

    syntax::ValueExpr Elaborator::proof_syntax_as_value(syntax::ProofExpr const &expression) {
        syntax::ValueExpr result;
        result.span = expression.span;
        result.node_id = expression.node_id;
        if (expression.kind == syntax::ProofExpr::Kind::name) {
            if (expression.name == "true" || expression.name == "false") {
                result.kind = syntax::ValueExpr::Kind::boolean;
                result.boolean_value = expression.name == "true";
            }
            else {
                result.kind = syntax::ValueExpr::Kind::name;
                result.name = expression.name;
            }
            return result;
        }
        if (expression.kind != syntax::ProofExpr::Kind::application || !expression.using_coeffects.empty())
            reject(expression.span, "expected a value argument, found proof syntax `" + print_proof(expression) + "`");
        result.kind = syntax::ValueExpr::Kind::call;
        result.name = expression.name;
        result.call_argument_end = expression.call_argument_end;
        std::size_t value_index = 0;
        std::size_t proof_index = 0;
        for (auto kind : expression.argument_kinds) {
            if (kind == syntax::ProofExpr::ArgumentKind::value)
                result.elements.push_back(expression.value_arguments[value_index++]);
            else
                result.elements.push_back(proof_syntax_as_value(expression.proof_arguments[proof_index++]));
        }
        return result;
    }
    syntax::ValueExpr Elaborator::positional_value_argument(syntax::ProofExpr const &expression, std::size_t position) {
        std::size_t value_index = 0;
        std::size_t proof_index = 0;
        for (std::size_t i = 0; i < expression.argument_kinds.size(); ++i) {
            if (expression.argument_kinds[i] == syntax::ProofExpr::ArgumentKind::value) {
                if (i == position)
                    return expression.value_arguments[value_index];
                ++value_index;
            }
            else {
                if (i == position)
                    return proof_syntax_as_value(expression.proof_arguments[proof_index]);
                ++proof_index;
            }
        }
        throw std::logic_error("proof call positional value argument is out of range");
    }
    syntax::ProofExpr const &Elaborator::positional_proof_argument(syntax::ProofExpr const &expression,
                                                                   std::size_t position) {
        std::size_t value_index = 0;
        std::size_t proof_index = 0;
        for (std::size_t i = 0; i < expression.argument_kinds.size(); ++i) {
            if (expression.argument_kinds[i] == syntax::ProofExpr::ArgumentKind::proof) {
                if (i == position)
                    return expression.proof_arguments[proof_index];
                ++proof_index;
            }
            else {
                if (i == position)
                    reject(expression.value_arguments[value_index].span,
                           "expected a proof argument in `" + print_proof(expression) + "`");
                ++value_index;
            }
        }
        throw std::logic_error("proof call positional proof argument is out of range");
    }
    proof_model::Type Elaborator::proof_model_type(IdentityType const &type) {
        return {sort(type.carrier).id(), Z3_get_ast_id(context_, type.left), Z3_get_ast_id(context_, type.right)};
    }
    std::string Elaborator::proof_model_type_key(proof_model::Type const &type) {
        return std::to_string(type.carrier) + ":" + std::to_string(type.left) + ":" + std::to_string(type.right);
    }
    void Elaborator::collect_proof_model_production(ProofCandidate const &candidate, proof_model::Grammar &grammar,
                                                    std::set<std::string> &seen) {
        if (!candidate.type)
            throw std::logic_error("typed proof candidate lost its result type");
        proof_model::Production production;
        production.result = proof_model_type(*candidate.type);
        std::string key;
        if (candidate.open) {
            production.kind = proof_model::ProductionKind::open;
            production.source = "?";
            key = "open:" + proof_model_type_key(production.result);
        }
        else if (candidate.local_proof) {
            production.kind = proof_model::ProductionKind::local;
            production.source = candidate.source;
            key = "local:" + candidate.source + ":" + proof_model_type_key(production.result);
        }
        else if (!candidate.proof_function) {
            production.kind = proof_model::ProductionKind::reflexivity;
            production.source = candidate.source;
            key = "refl:" + candidate.source + ":" + proof_model_type_key(production.result);
        }
        else {
            production.kind = proof_model::ProductionKind::application;
            production.function = *candidate.proof_function;
            production.index_arguments = candidate.index_arguments;
            auto function = proof_functions_.find(production.function);
            if (function == proof_functions_.end())
                throw std::logic_error("proof candidate names an unknown proof function");
            for (auto const &parameter : function->second->proof_parameters)
                production.coeffects.push_back(parameter.name);
            key = "apply:" + production.function;
            for (auto const &index : production.index_arguments)
                key += ":index:" + index;
            key += ":result:" + proof_model_type_key(production.result);
            for (auto const &child : candidate.children) {
                if (!child.type)
                    throw std::logic_error("typed proof application lost a child type");
                production.arguments.push_back(proof_model_type(*child.type));
                key += ":argument:" + proof_model_type_key(production.arguments.back());
            }
        }
        if (seen.insert(key).second)
            grammar.productions.push_back(std::move(production));
        for (auto const &child : candidate.children)
            collect_proof_model_production(child, grammar, seen);
    }
    proof_model::Grammar Elaborator::make_proof_model_grammar(std::vector<ProofCandidate> const &candidates,
                                                              IdentityType const &expected) {
        proof_model::Grammar grammar;
        grammar.id = "hole-" + std::to_string(proof_model_index_++);
        grammar.expected = proof_model_type(expected);
        grammar.max_cost = options_.proof_search_cost;
        auto preferred =
            std::max_element(candidates.begin(), candidates.end(), [](auto const &left, auto const &right) {
                if (left.complete != right.complete)
                    return !left.complete && right.complete;
                if (left.closed_frontier != right.closed_frontier)
                    return left.closed_frontier < right.closed_frontier;
                return left.cost > right.cost;
            });
        grammar.preferred_complete = preferred->complete;
        grammar.preferred_closed_frontier = preferred->closed_frontier;
        grammar.preferred_open_leaves = preferred->open_leaves;
        grammar.preferred_cost = preferred->cost;
        grammar.preferred_source = preferred->source;
        std::set<std::string> seen;
        if (options_.synthesize_partial_proofs)
            collect_proof_model_production(*preferred, grammar, seen);
        else
            for (auto const &candidate : candidates)
                collect_proof_model_production(candidate, grammar, seen);
        return grammar;
    }
    ProofEvidence Elaborator::elaborate_proof_application(
        syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax, SemanticProofType expected,
        ValueEnvironment const &values, ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
        std::vector<z3::expr> const &absorbed, std::string name, std::string const &run) {
        auto found = proof_functions_.find(expression.name);
        if (found == proof_functions_.end()) {
            if (functions_.contains(expression.name))
                reject(expression.span, "value function `" + expression.name + "` cannot inhabit a proof");
            reject(expression.span, "unknown proof function `" + expression.name + "`");
        }
        syntax::ProofFunctionDecl const &function = *found->second;
        if (expression.argument_kinds.size() != function.parameters.size())
            reject(expression.span, "proof function `" + expression.name + "` expects " +
                                        std::to_string(function.parameters.size()) + " value arguments");

        ValueEnvironment indices;
        std::vector<syntax::ValueExpr> index_arguments;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            syntax::ValueExpr argument_syntax = positional_value_argument(expression, i);
            ValueTerm argument = elaborate_value(argument_syntax, values, proofs, proof_order, absorbed);
            ValueKind expected_kind = kind_of(function.parameters[i].type);
            if (argument.kind != expected_kind)
                reject(argument_syntax.span,
                       "index argument `" + function.parameters[i].name + "` has the wrong value type");
            indices.emplace(function.parameters[i].name, std::move(argument));
            index_arguments.push_back(std::move(argument_syntax));
        }
        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        SemanticProofType result_type =
            elaborate_proof_type(function.result_type, indices, no_proofs, no_proof_order, no_absorbed);
        if (!same_type(context_, result_type, expected))
            reject(expression.span, "proof application `" + print_proof(expression) + "` has the wrong result type");

        std::vector<std::string> index_sources;
        for (auto const &argument : expression.value_arguments)
            index_sources.push_back(print_value(argument));
        std::vector<std::string> argument_sources;
        std::vector<std::optional<std::string>> named_arguments;
        std::vector<syntax::SourceSpan> argument_spans;
        bool arguments_complete = true;
        auto record_coeffect = [&](syntax::CoeffectParameter const &coeffect, std::string const &proof_source,
                                   bool explicit_choice) {
            ++result_.coeffects_resolved;
            output_ << "resolved coeffect: " << expression.name << '.' << coeffect.name << " <- " << proof_source
                    << (explicit_choice ? " (explicit)" : " (lexical search)") << '\n';
            if (!rainfall_)
                return;
            rainfall_->record("derive", "coeffect.demand.instantiate", {"proof-call:" + expression.name},
                              "fine.proof-elaborator",
                              "Value arguments instantiate the proof function's virtual evidence demand",
                              {RainfallRecorder::string_field("function", expression.name),
                               RainfallRecorder::string_field("coeffect", coeffect.name),
                               RainfallRecorder::string_field("proof_type", print_proof_type(coeffect.type)),
                               RainfallRecorder::boolean_field("proof_function", true)});
            rainfall_->record("derive", "coeffect.resolve", {"proof-call:" + expression.name},
                              "fine.lexical-proof-search",
                              "Named evidence or exact caller-local evidence satisfies the proof function coeffect",
                              {RainfallRecorder::string_field("function", expression.name),
                               RainfallRecorder::string_field("coeffect", coeffect.name),
                               RainfallRecorder::string_field("proof", proof_source),
                               RainfallRecorder::string_field("mode", explicit_choice ? "explicit" : "exact-local"),
                               RainfallRecorder::boolean_field("proof_function", true)});
            rainfall_->record("derive", "coeffect.use", {"proof-call:" + expression.name}, "fine.proof-context",
                              "Resolved proof evidence is supplied virtually to the proof function",
                              {RainfallRecorder::string_field("function", expression.name),
                               RainfallRecorder::string_field("coeffect", coeffect.name),
                               RainfallRecorder::string_field("proof", proof_source),
                               RainfallRecorder::boolean_field("runtime_argument_created", false),
                               RainfallRecorder::boolean_field("proof_function", true)});
        };
        std::map<std::string, std::size_t> explicit_arguments;
        for (std::size_t i = 0; i < expression.using_coeffects.size(); ++i) {
            if (!explicit_arguments.emplace(expression.using_coeffects[i], i).second)
                reject(expression.using_spans[i],
                       "duplicate explicit coeffect `" + expression.using_coeffects[i] + "`");
        }
        std::vector<std::pair<std::string, std::string>> chosen_locals;
        for (std::size_t i = 0; i < function.proof_parameters.size(); ++i) {
            SemanticProofType parameter_type = elaborate_proof_type(function.proof_parameters[i].type, indices,
                                                                    no_proofs, no_proof_order, no_absorbed);
            auto explicit_found = explicit_arguments.find(function.proof_parameters[i].name);
            if (explicit_found != explicit_arguments.end()) {
                auto const &argument_expression = expression.using_proofs[explicit_found->second];
                if (argument_expression.kind == syntax::ProofExpr::Kind::hole && !options_.validate_partial_proofs &&
                    !options_.synthesize_partial_proofs)
                    reject(argument_expression.span, "nested proof holes are not admitted in this slice");
                std::string argument_proof_source;
                std::string argument_type_source;
                if (rainfall_ && argument_expression.kind == syntax::ProofExpr::Kind::hole) {
                    argument_proof_source = rainfall_->source_node(argument_expression.node_id,
                                                                   argument_expression.span, "proof.expression.hole");
                    argument_type_source =
                        rainfall_->source_node(function.proof_parameters[i].type.node_id,
                                               function.proof_parameters[i].type.span, "proof-type.identity");
                }
                ProofEvidence argument = elaborate_any_proof(argument_expression, function.proof_parameters[i].type,
                                                             std::move(parameter_type), values, proofs, proof_order,
                                                             absorbed, name + "." + function.proof_parameters[i].name,
                                                             run, argument_proof_source, argument_type_source);
                arguments_complete = arguments_complete && argument.complete;
                argument_sources.push_back(print_proof(argument_expression));
                named_arguments.push_back(argument_expression.kind == syntax::ProofExpr::Kind::name
                                              ? std::optional<std::string>(argument_expression.name)
                                              : std::nullopt);
                argument_spans.push_back(argument_expression.span);
                record_coeffect(function.proof_parameters[i], argument_sources.back(), true);
                explicit_arguments.erase(explicit_found);
                continue;
            }
            if (options_.require_explicit_coeffects)
                reject(expression.span, "implicit coeffect `" + expression.name + "." +
                                            function.proof_parameters[i].name + "` remains after materialization");
            std::string proof_name;
            for (auto const &candidate : proof_order) {
                auto candidate_found = proofs.find(candidate);
                if (candidate_found != proofs.end() &&
                    same_type(context_, candidate_found->second.type, parameter_type)) {
                    proof_name = candidate;
                    break;
                }
            }
            if (proof_name.empty())
                reject(expression.span, "missing caller proof for coeffect `" + expression.name + "." +
                                            function.proof_parameters[i].name + " : " +
                                            print_proof_type(function.proof_parameters[i].type) + "`");
            auto const &argument = proofs.at(proof_name);
            arguments_complete = arguments_complete && argument.complete;
            argument_sources.push_back(proof_name);
            named_arguments.push_back(proof_name);
            argument_spans.push_back(argument.span);
            chosen_locals.emplace_back(function.proof_parameters[i].name, proof_name);
            record_coeffect(function.proof_parameters[i], proof_name, false);
        }
        if (!explicit_arguments.empty())
            reject(expression.span, "proof call supplies unknown coeffect `" + explicit_arguments.begin()->first + "`");
        if (expression.using_coeffects.empty() && !chosen_locals.empty()) {
            std::ostringstream insertion;
            insertion << " using [";
            for (std::size_t i = 0; i < chosen_locals.size(); ++i) {
                if (i)
                    insertion << ", ";
                insertion << chosen_locals[i].first << " = " << chosen_locals[i].second;
            }
            insertion << ']';
            request_materialization(syntax::ConcreteRange::empty_at(expression.call_argument_end), insertion.str(),
                                    expression.span);
        }

        bool induction_hypothesis_use = active_inductive_function_ == &function;
        std::string recursive_evidence;
        std::string recursive_parent;
        if (induction_hypothesis_use) {
            auto parameter = std::find_if(function.proof_parameters.begin(), function.proof_parameters.end(),
                                          [&](syntax::CoeffectParameter const &candidate) {
                                              return candidate.name == *function.induction_parameter;
                                          });
            if (parameter == function.proof_parameters.end())
                throw std::logic_error("active induction parameter disappeared");
            std::size_t position = static_cast<std::size_t>(parameter - function.proof_parameters.begin());
            if (!named_arguments[position])
                reject(argument_spans[position], "recursive proof call `" + function.name +
                                                     "` must use a named recursive constructor field for `" +
                                                     *function.induction_parameter + "`");
            auto evidence = proofs.find(*named_arguments[position]);
            if (evidence == proofs.end() || !evidence->second.structural_root ||
                *evidence->second.structural_root != *function.induction_parameter)
                reject(argument_spans[position], "recursive proof call `" + function.name +
                                                     "` does not descend through a proof field of induction "
                                                     "parameter `" +
                                                     *function.induction_parameter + "`");
            recursive_evidence = *named_arguments[position];
            recursive_parent = evidence->second.structural_parent.value_or("");
        }

        if (rainfall_ && induction_hypothesis_use)
            rainfall_->record(
                "derive", "proof.induction.hypothesis.use", {"proof-function:" + run}, "fine.proof-elaborator",
                "A recursive source application uses the induction hypothesis attached to an exact smaller "
                "constructor field; no runtime call exists",
                {RainfallRecorder::string_field("function", function.name),
                 RainfallRecorder::string_field("induction_parameter", *function.induction_parameter),
                 RainfallRecorder::string_field("parent_evidence", recursive_parent),
                 RainfallRecorder::string_field("recursive_evidence", recursive_evidence),
                 RainfallRecorder::string_field("body", print_proof(expression)),
                 RainfallRecorder::boolean_field("runtime_call_created", false)});
        else if (rainfall_)
            rainfall_->record(
                "derive", "proof.function.apply", {"run:" + run}, "fine.proof-elaborator",
                "A named proof-level function is applied to checked virtual evidence; no runtime call exists",
                {RainfallRecorder::string_field("function", function.name),
                 RainfallRecorder::string_field("body", print_proof(expression)),
                 RainfallRecorder::raw_field("index_arguments", RainfallRecorder::string_array(index_sources)),
                 RainfallRecorder::raw_field("proof_arguments", RainfallRecorder::string_array(argument_sources)),
                 RainfallRecorder::boolean_field("runtime_call_created", false)});
        std::vector<syntax::ValueExpr> index_syntax;
        std::string left_source;
        std::string right_source;
        if (expected_syntax.kind == syntax::ProofType::Kind::identity) {
            left_source = print_value(expected_syntax.left);
            right_source = print_value(expected_syntax.right);
        }
        else {
            index_syntax = expected_syntax.arguments;
        }
        return {std::move(name),
                std::move(expected),
                induction_hypothesis_use ? "induction-hypothesis:" + function.name : "apply:" + function.name,
                expression.span,
                std::move(left_source),
                std::move(right_source),
                std::move(index_syntax),
                std::nullopt,
                std::nullopt,
                arguments_complete};
    }
    std::vector<ProofCandidate> Elaborator::enumerate_proof_candidates(
        syntax::ProofType const &expected_syntax, IdentityType const &expected, std::string const &left_source,
        std::string const &right_source, ValueEnvironment const &values, ProofEnvironment const &proofs,
        std::vector<std::string> const &proof_order, std::vector<z3::expr> const &absorbed, std::size_t budget) {
        bool allow_open = options_.synthesize_partial_proofs;
        auto open_candidate = [&]() {
            ProofCandidate candidate;
            candidate.source = "?";
            candidate.production = "open";
            candidate.cost = 0;
            candidate.type = expected;
            candidate.open = true;
            candidate.complete = false;
            candidate.closed_frontier = 0;
            candidate.open_leaves = 1;
            return candidate;
        };
        if (budget == 0)
            return allow_open ? std::vector<ProofCandidate>{open_candidate()} : std::vector<ProofCandidate>{};
        std::vector<ProofCandidate> candidates;
        for (auto const &candidate_name : proof_order) {
            auto found = proofs.find(candidate_name);
            if (found != proofs.end()) {
                auto identity = std::get_if<IdentityType>(&found->second.type);
                if (identity && same_type(context_, *identity, expected))
                    candidates.push_back(
                        {candidate_name, "exact-local", candidate_name, std::nullopt, {}, {}, 1, expected, {}});
            }
        }
        if (same_ast(context_, expected.left, expected.right))
            candidates.push_back(
                {"refl(" + left_source + ")", "refl", std::nullopt, std::nullopt, {}, {}, 1, expected, {}});

        for (auto const &function_name : proof_function_order_) {
            syntax::ProofFunctionDecl const &function = *proof_functions_.at(function_name);
            auto instantiations =
                infer_value_arguments(function, expected, left_source, right_source, proofs, proof_order);
            for (auto const &instantiation : instantiations) {
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                std::vector<std::vector<ProofCandidate>> argument_frontiers;
                bool applicable = true;
                for (auto const &parameter : function.proof_parameters) {
                    IdentityType argument_type = elaborate_identity(parameter.type, instantiation.values, no_proofs,
                                                                    no_proof_order, no_absorbed);
                    std::string argument_left = print_value_substituted(parameter.type.left, instantiation.sources);
                    std::string argument_right = print_value_substituted(parameter.type.right, instantiation.sources);
                    auto frontier =
                        enumerate_proof_candidates(parameter.type, argument_type, argument_left, argument_right, values,
                                                   proofs, proof_order, absorbed, budget - 1);
                    if (frontier.empty()) {
                        applicable = false;
                        break;
                    }
                    argument_frontiers.push_back(std::move(frontier));
                }
                if (!applicable)
                    continue;

                std::vector<std::vector<ProofCandidate>> combinations(1);
                for (auto const &frontier : argument_frontiers) {
                    std::vector<std::vector<ProofCandidate>> next;
                    for (auto const &combination : combinations)
                        for (auto const &argument : frontier) {
                            auto extended = combination;
                            extended.push_back(argument);
                            next.push_back(std::move(extended));
                        }
                    combinations = std::move(next);
                }
                for (auto const &arguments : combinations) {
                    std::size_t cost = 1;
                    std::vector<std::string> argument_sources;
                    for (auto const &argument : arguments) {
                        cost += argument.cost;
                        argument_sources.push_back(argument.source);
                    }
                    if (cost > budget)
                        continue;
                    std::ostringstream source;
                    source << function.name << '(';
                    std::vector<std::string> index_arguments;
                    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                        if (i)
                            source << ", ";
                        std::string const &argument = instantiation.sources.at(function.parameters[i].name);
                        source << argument;
                        index_arguments.push_back(argument);
                    }
                    source << ')';
                    if (!argument_sources.empty()) {
                        source << " using [";
                        for (std::size_t i = 0; i < argument_sources.size(); ++i) {
                            if (i)
                                source << ", ";
                            source << function.proof_parameters[i].name << " = " << argument_sources[i];
                        }
                        source << ']';
                    }
                    bool complete = true;
                    std::size_t closed_frontier = 0;
                    std::size_t open_leaves = 0;
                    for (auto const &argument : arguments) {
                        complete = complete && argument.complete;
                        open_leaves += argument.open_leaves;
                        closed_frontier += argument.complete ? 1 : argument.closed_frontier;
                    }
                    ProofCandidate candidate{source.str(),
                                             "proof-application",
                                             std::nullopt,
                                             function.name,
                                             std::move(index_arguments),
                                             std::move(argument_sources),
                                             cost,
                                             expected,
                                             arguments};
                    candidate.complete = complete;
                    candidate.closed_frontier = complete ? 1 : closed_frontier;
                    candidate.open_leaves = open_leaves;
                    candidates.push_back(std::move(candidate));
                }
            }
        }
        if (allow_open)
            candidates.push_back(open_candidate());
        return candidates;
    }
    ProofEvidence Elaborator::elaborate_proof(syntax::ProofExpr const &expression,
                                              syntax::ProofType const &expected_syntax, IdentityType expected,
                                              ValueEnvironment const &values, ProofEnvironment const &proofs,
                                              std::vector<std::string> const &proof_order,
                                              std::vector<z3::expr> const &absorbed, std::string name,
                                              std::string const &run, std::string const &proof_source,
                                              std::string const &type_source) {
        if (expression.kind == syntax::ProofExpr::Kind::name) {
            auto found = proofs.find(expression.name);
            if (found == proofs.end())
                reject(expression.span, "unknown proof `" + expression.name + "`");
            auto identity = std::get_if<IdentityType>(&found->second.type);
            if (!identity || !same_type(context_, *identity, expected))
                reject(expression.span, "proof `" + expression.name + "` has the wrong identity type");
            return {std::move(name),
                    std::move(expected),
                    "alias:" + expression.name,
                    expression.span,
                    print_value(expected_syntax.left),
                    print_value(expected_syntax.right)};
        }
        if (expression.kind == syntax::ProofExpr::Kind::reflexivity) {
            ValueTerm witness = elaborate_value(expression.value, values, proofs, proof_order, absorbed);
            if (witness.kind != expected.carrier || !same_ast(context_, witness.expression, expected.left) ||
                !same_ast(context_, witness.expression, expected.right))
                reject(expression.span, "`refl` requires both identity endpoints to elaborate to its exact value");
            return {std::move(name),
                    std::move(expected),
                    "refl",
                    expression.span,
                    print_value(expected_syntax.left),
                    print_value(expected_syntax.right)};
        }
        if (expression.kind == syntax::ProofExpr::Kind::application)
            return elaborate_proof_application(expression, expected_syntax, SemanticProofType(std::move(expected)),
                                               values, proofs, proof_order, absorbed, std::move(name), run);

        if (options_.validate_partial_proofs)
            return {std::move(name),
                    std::move(expected),
                    "open",
                    expression.span,
                    print_value(expected_syntax.left),
                    print_value(expected_syntax.right),
                    {},
                    std::nullopt,
                    std::nullopt,
                    false};

        if (options_.require_materialized_proofs)
            reject(expression.span, "proof hole remains after materialization");

        if (options_.live_iterative_proof_search) {
            if (live_identity_holes_++ != 0)
                reject(expression.span,
                       "live iterative search currently supports one identity hole per source episode");
            ProofCandidate selected;
            std::size_t budget = options_.live_proof_search_start - 1;
            std::string last_observed_source;
            for (;;) {
                ++budget;
                std::vector<ProofCandidate> frontier = enumerate_proof_candidates(
                    expected_syntax, expected, print_value(expected_syntax.left), print_value(expected_syntax.right),
                    values, proofs, proof_order, absorbed, budget);
                if (frontier.empty())
                    reject(expression.span, "live proof search produced an empty typed frontier");
                proof_model::Grammar grammar = make_proof_model_grammar(frontier, expected);
                grammar.max_cost = budget;
                proof_model::Result model_selection = proof_model::select(
                    context_, grammar,
                    [&](proof_model::Grammar const &observed_grammar, z3::context &model_context, z3::expr const &model,
                        proof_model::Result const &result) {
#ifdef FINE_HAS_LIVE_LIFT
                        if (options_.live_lift && result.source != last_observed_source) {
                            std::ostringstream metadata;
                            metadata << expression.span.begin.offset << '\t' << expression.span.end.offset << '\t'
                                     << budget << '\t' << (result.complete ? 1 : 0) << '\t' << result.closed_frontier
                                     << '\t' << result.open_leaves << '\t' << result.cost;
                            std::string expected_source = result.source;
                            options_.live_lift->observe(
                                metadata.str(), model_context, model,
                                [observed_grammar, expected_source](z3::context &copied_context,
                                                                    z3::expr const &copied_model) {
                                    std::string lifted =
                                        proof_model::lift_model_term(copied_context, copied_model, observed_grammar);
                                    if (lifted != expected_source)
                                        throw std::runtime_error("translated live proof model changed its Fine source");
                                    return lifted;
                                });
                            last_observed_source = result.source;
                        }
#else
                        (void)observed_grammar;
                        (void)model_context;
                        (void)model;
                        (void)result;
#endif
                    });
                if (model_selection.status != proof_model::Status::sat)
                    reject(expression.span,
                           "live Z3 proof selector failed for `" + name + "`: " + model_selection.reason);
                if (model_selection.source != grammar.preferred_source)
                    reject(expression.span, "live Z3 proof model did not reproduce Fine's preferred source tree");
                auto found = std::find_if(frontier.begin(), frontier.end(), [&](ProofCandidate const &item) {
                    return item.source == model_selection.source && item.cost == model_selection.cost &&
                           item.complete == model_selection.complete &&
                           item.closed_frontier == model_selection.closed_frontier &&
                           item.open_leaves == model_selection.open_leaves;
                });
                if (found == frontier.end())
                    reject(expression.span, "live Z3 proof model lifted outside its exact typed frontier");
                selected = *found;
                if (rainfall_)
                    rainfall_->record("derive", "proof.search.live.model", {"run:" + run}, "fine.proof-model-selector",
                                      "One bounded typed frontier produced the next exact source checkpoint while the "
                                      "following frontier may continue independently of presentation",
                                      {RainfallRecorder::string_field("body", selected.source),
                                       RainfallRecorder::number_field("budget", budget),
                                       RainfallRecorder::number_field("cost", selected.cost),
                                       RainfallRecorder::boolean_field("complete", selected.complete),
                                       RainfallRecorder::number_field("closed_frontier", selected.closed_frontier),
                                       RainfallRecorder::number_field("open_leaves", selected.open_leaves)});
                if (selected.complete ||
                    (options_.live_proof_search_limit != 0 && budget >= options_.live_proof_search_limit))
                    break;
            }

            request_materialization(syntax::ConcreteRange::from_span(expression.span), selected.source,
                                    expression.span);
            if (selected.complete)
                ++result_.proof_holes_filled;
            else {
                ++result_.proof_holes_checkpointed;
                result_.checkpoint_open = true;
            }
            output_ << (selected.complete ? "filled proof hole: " : "checkpointed proof hole: ") << name << " <- "
                    << selected.source << " (live Z3 iterative search)\n";
            return {std::move(name),
                    std::move(expected),
                    selected.complete ? "search:z3-live:complete" : "search:z3-live:open",
                    expression.span,
                    print_value(expected_syntax.left),
                    print_value(expected_syntax.right),
                    {},
                    std::nullopt,
                    std::nullopt,
                    selected.complete};
        }

        std::vector<ProofCandidate> candidates = enumerate_proof_candidates(
            expected_syntax, expected, print_value(expected_syntax.left), print_value(expected_syntax.right), values,
            proofs, proof_order, absorbed, options_.proof_search_cost);

        std::string proposition;
        std::string hole = "proof-hole:" + proof_source;
        std::vector<std::string> candidate_events;
        if (rainfall_) {
            proposition = rainfall_->term(expected.left == expected.right, "proof-hole-proposition");
            rainfall_->record(
                "object", "proof.search.open", {"run:" + run, hole}, "fine.typed-proof-search",
                "A source proof hole opens with a finite grammar determined by its expected identity type",
                {RainfallRecorder::string_field("id", hole), RainfallRecorder::string_field("source", proof_source),
                 RainfallRecorder::string_field("type_source", type_source),
                 RainfallRecorder::string_field("binding", name),
                 RainfallRecorder::string_field("expected_type", print_identity(expected_syntax)),
                 RainfallRecorder::string_field("proposition", proposition),
                 RainfallRecorder::raw_field("grammar",
                                             options_.synthesize_partial_proofs
                                                 ? "[\"exact-local\",\"refl\",\"proof-application\",\"open\"]"
                                                 : "[\"exact-local\",\"refl\",\"proof-application\"]"),
                 RainfallRecorder::number_field("max_cost", options_.proof_search_cost),
                 RainfallRecorder::boolean_field("checkpoint_mode", options_.synthesize_partial_proofs),
                 RainfallRecorder::boolean_field("ill_typed_candidates_enumerated", false)});
            for (auto const &candidate : candidates) {
                std::vector<RainfallField> data = {
                    RainfallRecorder::string_field("hole", hole),
                    RainfallRecorder::string_field("body", candidate.source),
                    RainfallRecorder::string_field("production", candidate.production),
                    RainfallRecorder::string_field("expected_type", print_identity(expected_syntax)),
                    RainfallRecorder::boolean_field("exact_type", true),
                    RainfallRecorder::boolean_field("runtime_value_created", false),
                    RainfallRecorder::boolean_field("complete", candidate.complete),
                    RainfallRecorder::number_field("closed_frontier", candidate.closed_frontier),
                    RainfallRecorder::number_field("open_leaves", candidate.open_leaves),
                };
                if (candidate.local_proof)
                    data.push_back(RainfallRecorder::string_field("proof", *candidate.local_proof));
                if (candidate.proof_function) {
                    data.push_back(RainfallRecorder::string_field("function", *candidate.proof_function));
                    data.push_back(RainfallRecorder::raw_field(
                        "index_arguments", RainfallRecorder::string_array(candidate.index_arguments)));
                    data.push_back(RainfallRecorder::raw_field(
                        "proof_arguments", RainfallRecorder::string_array(candidate.proof_arguments)));
                }
                data.push_back(RainfallRecorder::number_field("cost", candidate.cost));
                candidate_events.push_back(rainfall_->record(
                    "derive", "proof.search.candidate", {"run:" + run, hole}, "fine.typed-proof-search",
                    "A well-typed Fine proof production entered the finite frontier; mismatched productions "
                    "are absent",
                    data));
            }
        }

        if (candidates.empty())
            reject(expression.span, "proof hole `" + name +
                                        "` has no well-typed candidate in bounded grammar "
                                        "[exact-local, refl, proof-application]");

        std::size_t selected_index = 0;
        std::optional<proof_model::Grammar> model_grammar;
        std::optional<proof_model::Result> model_selection;
        if (options_.proof_selector == ProofSelector::z3_model) {
            model_grammar = make_proof_model_grammar(candidates, expected);
            model_selection = proof_model::select(context_, *model_grammar);
            if (model_selection->status != proof_model::Status::sat)
                reject(expression.span,
                       "Z3 proof model selector failed for `" + name + "`: " + model_selection->reason);
            if (options_.synthesize_partial_proofs && model_selection->source != model_grammar->preferred_source)
                reject(expression.span, "Z3 proof model did not reproduce Fine's preferred partial source tree");
            auto found = std::find_if(candidates.begin(), candidates.end(), [&](ProofCandidate const &item) {
                return item.source == model_selection->source && item.cost == model_selection->cost &&
                       item.complete == model_selection->complete &&
                       item.closed_frontier == model_selection->closed_frontier &&
                       item.open_leaves == model_selection->open_leaves;
            });
            if (found == candidates.end())
                reject(expression.span, "Z3 proof model lifted a tree outside the deterministic Fine frontier: `" +
                                            model_selection->source + "`");
            selected_index = static_cast<std::size_t>(found - candidates.begin());
        }

        ProofCandidate const &selected = candidates[selected_index];
        request_materialization(syntax::ConcreteRange::from_span(expression.span), selected.source, expression.span);
        if (selected.complete)
            ++result_.proof_holes_filled;
        else
            ++result_.proof_holes_checkpointed;
        if (rainfall_) {
            if (model_grammar && model_selection) {
                std::vector<std::string> productions;
                for (auto const &production : model_grammar->productions) {
                    if (production.kind == proof_model::ProductionKind::application) {
                        std::ostringstream description;
                        description << "apply:" << production.function << '(';
                        for (std::size_t i = 0; i < production.index_arguments.size(); ++i) {
                            if (i)
                                description << ", ";
                            description << production.index_arguments[i];
                        }
                        description << ")/" << production.arguments.size();
                        productions.push_back(description.str());
                    }
                    else {
                        std::string prefix = production.kind == proof_model::ProductionKind::open    ? "open:"
                                             : production.kind == proof_model::ProductionKind::local ? "local:"
                                                                                                     : "refl:";
                        productions.push_back(prefix + production.source);
                    }
                }
                std::string grammar_event = rainfall_->record(
                    "object", "proof.model.grammar", {"run:" + run, hole}, "fine.proof-model-selector",
                    options_.synthesize_partial_proofs
                        ? "Fine ranks the typed partial frontier, then compacts the preferred tree's productions into "
                          "a ground recursive datatype"
                        : "The exact deterministic Fine frontier is compacted into ground recursive datatype "
                          "productions without changing its bound or source owners",
                    {RainfallRecorder::string_field("hole", hole),
                     RainfallRecorder::string_field("grammar", model_grammar->id),
                     RainfallRecorder::number_field("max_cost", model_grammar->max_cost),
                     RainfallRecorder::boolean_field("preferred_complete", model_grammar->preferred_complete),
                     RainfallRecorder::number_field("preferred_closed_frontier",
                                                    model_grammar->preferred_closed_frontier),
                     RainfallRecorder::number_field("preferred_open_leaves", model_grammar->preferred_open_leaves),
                     RainfallRecorder::number_field("preferred_cost", model_grammar->preferred_cost),
                     RainfallRecorder::string_field("preferred_source", model_grammar->preferred_source),
                     RainfallRecorder::raw_field("productions", RainfallRecorder::string_array(productions)),
                     RainfallRecorder::raw_field("reference_candidates",
                                                 RainfallRecorder::string_array(candidate_events))});
                std::string solve_event = rainfall_->record(
                    "derive", "proof.model.solve", {"run:" + run, hole, grammar_event}, "fine.proof-model-selector",
                    "Z3 assigns the bounded recursive proof datatype a ground constructor tree",
                    {RainfallRecorder::string_field("hole", hole),
                     RainfallRecorder::string_field("grammar_event", grammar_event),
                     RainfallRecorder::string_field("grammar", model_grammar->id),
                     RainfallRecorder::string_field("status", "sat"),
                     RainfallRecorder::string_field("model_value", model_selection->model_value),
                     RainfallRecorder::number_field("cost", model_selection->cost),
                     RainfallRecorder::boolean_field("complete", model_selection->complete),
                     RainfallRecorder::number_field("closed_frontier", model_selection->closed_frontier),
                     RainfallRecorder::number_field("open_leaves", model_selection->open_leaves)});
                rainfall_->record("transform", "proof.model.lift", {"run:" + run, hole, solve_event},
                                  "fine.proof-model-selector",
                                  "The model constructor tree lifts to Fine source and matches one exact reference "
                                  "candidate before materialization",
                                  {RainfallRecorder::string_field("hole", hole),
                                   RainfallRecorder::string_field("solve_event", solve_event),
                                   RainfallRecorder::string_field("body", model_selection->source),
                                   RainfallRecorder::string_field("candidate", candidate_events[selected_index]),
                                   RainfallRecorder::boolean_field("complete", model_selection->complete),
                                   RainfallRecorder::boolean_field("in_reference_frontier", true),
                                   RainfallRecorder::boolean_field("reparse_required", true)});
            }
            std::string const &selected_event = candidate_events[selected_index];
            std::string selection = rainfall_->record(
                "transition", "proof.search.select", {"run:" + run, hole}, "fine.typed-proof-search",
                options_.proof_selector == ProofSelector::z3_model
                    ? "The Z3 datatype model selects one exact reference candidate for materialization"
                    : "The first deterministic well-typed source proof is selected for checking and "
                      "materialization",
                {RainfallRecorder::string_field("hole", hole),
                 RainfallRecorder::string_field("candidate", selected_event),
                 RainfallRecorder::string_field("body", selected.source),
                 RainfallRecorder::string_field("production", selected.production),
                 RainfallRecorder::boolean_field("complete", selected.complete)});
            std::vector<std::string> residual;
            for (std::size_t i = 0; i < candidate_events.size(); ++i)
                if (i != selected_index)
                    residual.push_back(candidate_events[i]);
            rainfall_->record(
                "transition", "proof.search.close", {"run:" + run, hole}, "fine.typed-proof-search",
                selected.complete
                    ? "The typed hole has a checked source witness and its unchosen finite frontier remains explicit"
                    : "The typed hole has a checked partial Fine term; every unresolved leaf remains explicit",
                {RainfallRecorder::string_field("hole", hole), RainfallRecorder::string_field("selection", selection),
                 RainfallRecorder::string_field("selected_candidate", selected_event),
                 RainfallRecorder::raw_field("residual_candidates", RainfallRecorder::string_array(residual)),
                 RainfallRecorder::string_field("status", selected.complete ? "selected" : "checkpointed"),
                 RainfallRecorder::boolean_field("materialization_requested", true)});
        }
        output_ << (selected.complete ? "filled proof hole: " : "checkpointed proof hole: ") << name << " <- "
                << selected.source << " ("
                << (options_.proof_selector == ProofSelector::z3_model ? "Z3 datatype model" : "typed search") << ")\n";
        std::string formation;
        if (selected.open)
            formation = "open";
        else if (selected.local_proof)
            formation = "exact-local:" + *selected.local_proof;
        else if (selected.proof_function)
            formation = "proof-application:" + *selected.proof_function;
        else
            formation = "refl";
        formation = (options_.proof_selector == ProofSelector::z3_model ? "search:z3-model:" : "search:") + formation;
        return {std::move(name),
                std::move(expected),
                std::move(formation),
                expression.span,
                print_value(expected_syntax.left),
                print_value(expected_syntax.right),
                {},
                std::nullopt,
                std::nullopt,
                selected.complete};
    }

}  // namespace fine::runtime_detail
