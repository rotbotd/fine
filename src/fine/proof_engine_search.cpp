#include "elaboration_internal.h"

#include <tuple>

// Bounded proof search: surface argument classification, named proof-function
// application, deterministic identity grammars, and Z3 model selection.
namespace fine::elaboration {

    namespace {
        std::string proof_model_type_json(proof_model::Type const &type) {
            return "{\"carrier\":" + std::to_string(type.carrier) + ",\"left\":" + std::to_string(type.left) +
                   ",\"right\":" + std::to_string(type.right) + "}";
        }

        std::string proof_model_kind(proof_model::ProductionKind kind) {
            switch (kind) {
            case proof_model::ProductionKind::open: return "open";
            case proof_model::ProductionKind::local: return "local";
            case proof_model::ProductionKind::reflexivity: return "refl";
            case proof_model::ProductionKind::application: return "proof-application";
            }
            throw std::logic_error("unknown proof model production kind");
        }

        std::string proof_model_productions_json(proof_model::Grammar const &grammar) {
            std::ostringstream result;
            result << '[';
            for (std::size_t i = 0; i < grammar.productions.size(); ++i) {
                if (i)
                    result << ',';
                auto const &production = grammar.productions[i];
                result << "{\"id\":" << i << ",\"kind\":" << RainfallRecorder::quote(proof_model_kind(production.kind))
                       << ",\"source\":" << RainfallRecorder::quote(production.source)
                       << ",\"function\":" << RainfallRecorder::quote(production.function)
                       << ",\"index_arguments\":" << RainfallRecorder::string_array(production.index_arguments)
                       << ",\"coeffects\":" << RainfallRecorder::string_array(production.coeffects)
                       << ",\"result\":" << proof_model_type_json(production.result) << ",\"arguments\":[";
                for (std::size_t argument = 0; argument < production.arguments.size(); ++argument) {
                    if (argument)
                        result << ',';
                    result << proof_model_type_json(production.arguments[argument]);
                }
                result << "]}";
            }
            result << ']';
            return result.str();
        }

        std::string proof_model_states_json(std::vector<proof_model::StateSummary> const &states) {
            std::ostringstream result;
            result << '[';
            for (std::size_t i = 0; i < states.size(); ++i) {
                if (i)
                    result << ',';
                auto const &state = states[i];
                result << "{\"id\":" << RainfallRecorder::quote(state.id)
                       << ",\"type\":" << proof_model_type_json(state.type) << ",\"cost\":" << state.cost
                       << ",\"complete\":" << (state.complete ? "true" : "false")
                       << ",\"closed_frontier\":" << state.closed_frontier << ",\"open_leaves\":" << state.open_leaves
                       << ",\"alternatives\":[";
                for (std::size_t alternative = 0; alternative < state.transitions.size(); ++alternative) {
                    if (alternative)
                        result << ',';
                    auto const &transition = state.transitions[alternative];
                    result << "{\"production\":" << transition.production
                           << ",\"children\":" << RainfallRecorder::string_array(transition.children) << '}';
                }
                result << "]}";
            }
            result << ']';
            return result.str();
        }
    }  // namespace

    ProofEngine::ProofEngine(ValueElaborator &values, std::ostream &output, ExecutionOptions const &options,
                             MaterializationSink &materializations)
        : values_(values), output_(output), options_(options), materializations_(materializations) {}

    syntax::ValueExpr ProofEngine::proof_syntax_as_value(syntax::ProofExpr const &expression) {
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
    syntax::ValueExpr ProofEngine::positional_value_argument(syntax::ProofExpr const &expression,
                                                             std::size_t position) {
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
    syntax::ProofExpr const &ProofEngine::positional_proof_argument(syntax::ProofExpr const &expression,
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
    proof_model::Type ProofEngine::proof_model_type(IdentityType const &type) {
        return {values_.sort(type.carrier).id(), Z3_get_ast_id(values_.context(), type.left),
                Z3_get_ast_id(values_.context(), type.right)};
    }
    std::string ProofEngine::proof_model_type_key(proof_model::Type const &type) {
        return std::to_string(type.carrier) + ":" + std::to_string(type.left) + ":" + std::to_string(type.right);
    }
    proof_model::Grammar
    ProofEngine::make_direct_proof_model_grammar(std::string const &grammar_id, IdentityType const &expected,
                                                 std::string const &left_source, std::string const &right_source,
                                                 ProofEnvironment const &proofs,
                                                 std::vector<std::string> const &proof_order, std::size_t budget) {
        proof_model::Grammar grammar;
        grammar.id = grammar_id;
        grammar.expected = proof_model_type(expected);
        grammar.max_cost = budget;
        grammar.rank_automatically = true;

        struct RankedProduction {
            std::string result_type;
            std::size_t category = 0;
            std::size_t primary_order = 0;
            std::size_t secondary_order = 0;
            std::string key;
            proof_model::Production production;
        };
        std::vector<RankedProduction> ranked_productions;
        std::set<std::string> seen_productions;
        auto add_production = [&](proof_model::Production production, std::string key, std::size_t category,
                                  std::size_t primary_order = 0, std::size_t secondary_order = 0) {
            if (seen_productions.insert(key).second)
                ranked_productions.push_back({proof_model_type_key(production.result), category, primary_order,
                                              secondary_order, std::move(key), std::move(production)});
        };
        std::set<std::string> visited;
        std::function<void(IdentityType const &, std::string const &, std::string const &, std::size_t)> visit;
        visit = [&](IdentityType const &wanted, std::string const &wanted_left, std::string const &wanted_right,
                    std::size_t remaining) {
            proof_model::Type wanted_model = proof_model_type(wanted);
            std::string state_key = proof_model_type_key(wanted_model) + ":left:" + wanted_left +
                                    ":right:" + wanted_right + ":budget:" + std::to_string(remaining);
            if (!visited.insert(std::move(state_key)).second)
                return;

            if (remaining > 0) {
                for (std::size_t proof_index = 0; proof_index < proof_order.size(); ++proof_index) {
                    std::string const &candidate_name = proof_order[proof_index];
                    auto found = proofs.find(candidate_name);
                    if (found == proofs.end())
                        continue;
                    auto identity = std::get_if<IdentityType>(&found->second.type);
                    if (!identity || !same_type(values_.context(), *identity, wanted))
                        continue;
                    proof_model::Production production;
                    production.kind = proof_model::ProductionKind::local;
                    production.source = candidate_name;
                    production.result = wanted_model;
                    add_production(std::move(production),
                                   "local:" + candidate_name + ":" + proof_model_type_key(wanted_model), 0,
                                   proof_index);
                }
                if (same_ast(values_.context(), wanted.left, wanted.right)) {
                    proof_model::Production production;
                    production.kind = proof_model::ProductionKind::reflexivity;
                    production.source = "refl(" + wanted_left + ")";
                    production.result = wanted_model;
                    add_production(std::move(production),
                                   "refl:" + wanted_left + ":" + proof_model_type_key(wanted_model), 1);
                }

                for (std::size_t function_index = 0; function_index < proof_function_order_.size(); ++function_index) {
                    std::string const &function_name = proof_function_order_[function_index];
                    syntax::ProofFunctionDecl const &function = *proof_functions_.at(function_name);
                    auto instantiations =
                        infer_value_arguments(function, wanted, wanted_left, wanted_right, proofs, proof_order);
                    for (std::size_t instantiation_index = 0; instantiation_index < instantiations.size();
                         ++instantiation_index) {
                        auto const &instantiation = instantiations[instantiation_index];
                        proof_model::Production production;
                        production.kind = proof_model::ProductionKind::application;
                        production.function = function.name;
                        production.result = wanted_model;
                        std::string production_key = "apply:" + function.name;
                        for (auto const &parameter : function.parameters) {
                            std::string const &source = instantiation.sources.at(parameter.name);
                            production.index_arguments.push_back(source);
                            production_key += ":index:" + source;
                        }
                        production_key += ":result:" + proof_model_type_key(wanted_model);

                        ProofEnvironment no_proofs;
                        std::vector<std::string> no_proof_order;
                        std::vector<z3::expr> no_absorbed;
                        struct Child {
                            IdentityType type;
                            std::string left;
                            std::string right;
                        };
                        std::vector<Child> children;
                        for (auto const &parameter : function.proof_parameters) {
                            IdentityType child_type = elaborate_identity(parameter.type, instantiation.values,
                                                                         no_proofs, no_proof_order, no_absorbed);
                            std::string child_left =
                                print_value_substituted(parameter.type.left, instantiation.sources);
                            std::string child_right =
                                print_value_substituted(parameter.type.right, instantiation.sources);
                            production.coeffects.push_back(parameter.name);
                            production.arguments.push_back(proof_model_type(child_type));
                            production_key += ":argument:" + proof_model_type_key(production.arguments.back());
                            children.push_back({std::move(child_type), std::move(child_left), std::move(child_right)});
                        }
                        add_production(std::move(production), std::move(production_key), 2, function_index,
                                       instantiation_index);
                        for (auto const &child : children)
                            visit(child.type, child.left, child.right, remaining - 1);
                    }
                }
            }

            if (options_.synthesize_partial_proofs) {
                proof_model::Production production;
                production.kind = proof_model::ProductionKind::open;
                production.source = "?";
                production.result = wanted_model;
                add_production(std::move(production), "open:" + proof_model_type_key(wanted_model), 3);
            }
        };
        visit(expected, left_source, right_source, budget);
        std::sort(ranked_productions.begin(), ranked_productions.end(), [](auto const &left, auto const &right) {
            return std::tie(left.result_type, left.category, left.primary_order, left.secondary_order, left.key) <
                   std::tie(right.result_type, right.category, right.primary_order, right.secondary_order, right.key);
        });
        grammar.productions.reserve(ranked_productions.size());
        for (auto &ranked : ranked_productions)
            grammar.productions.push_back(std::move(ranked.production));
        return grammar;
    }
    ProofEvidence ProofEngine::elaborate_proof_application(
        syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax, SemanticProofType expected,
        ValueEnvironment const &values, ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
        std::vector<z3::expr> const &absorbed, std::string name, std::string const &run) {
        auto found = proof_functions_.find(expression.name);
        if (found == proof_functions_.end()) {
            if (values_.has_function(expression.name))
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
            ValueTerm argument = values_.elaborate_value(argument_syntax, values, proofs, proof_order, absorbed);
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
        if (!same_type(values_.context(), result_type, expected))
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
            ++coeffects_resolved_;
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
                    same_type(values_.context(), candidate_found->second.type, parameter_type)) {
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
            materializations_.request_materialization(syntax::ConcreteRange::empty_at(expression.call_argument_end),
                                                      insertion.str(), expression.span);
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
    std::vector<ProofCandidate> ProofEngine::enumerate_proof_candidates(
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
                if (identity && same_type(values_.context(), *identity, expected))
                    candidates.push_back(
                        {candidate_name, "exact-local", candidate_name, std::nullopt, {}, {}, 1, expected, {}});
            }
        }
        if (same_ast(values_.context(), expected.left, expected.right))
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
    ProofEvidence ProofEngine::elaborate_proof(syntax::ProofExpr const &expression,
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
            if (!identity || !same_type(values_.context(), *identity, expected))
                reject(expression.span, "proof `" + expression.name + "` has the wrong identity type");
            return {std::move(name),
                    std::move(expected),
                    "alias:" + expression.name,
                    expression.span,
                    print_value(expected_syntax.left),
                    print_value(expected_syntax.right)};
        }
        if (expression.kind == syntax::ProofExpr::Kind::reflexivity) {
            ValueTerm witness = values_.elaborate_value(expression.value, values, proofs, proof_order, absorbed);
            if (witness.kind != expected.carrier || !same_ast(values_.context(), witness.expression, expected.left) ||
                !same_ast(values_.context(), witness.expression, expected.right))
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
            ProofCandidate selected;
            std::size_t budget = options_.live_proof_search_start - 1;
            std::string last_observed_source;
            std::string grammar_id = "direct-hole-" + std::to_string(proof_model_index_++);
            proof_model::IncrementalSelector model_selector;
            for (;;) {
                ++budget;
                proof_model::Grammar grammar =
                    make_direct_proof_model_grammar(grammar_id, expected, print_value(expected_syntax.left),
                                                    print_value(expected_syntax.right), proofs, proof_order, budget);
                proof_model::Result model_selection = model_selector.select(
                    values_.context(), grammar,
                    [&](proof_model::Grammar const &observed_grammar, z3::context &model_context, z3::expr const &model,
                        proof_model::Result const &result) {
#ifdef FINE_HAS_LIVE_LIFT
                        if (options_.live_lift && result.source != last_observed_source) {
                            std::ostringstream metadata;
                            metadata << expression.span.begin.offset << '\t' << expression.span.end.offset << '\t'
                                     << budget << '\t' << (result.complete ? 1 : 0) << '\t' << result.closed_frontier
                                     << '\t' << result.open_leaves << '\t' << result.cost << '\t' << name;
                            std::string expected_source = result.source;
                            std::vector<LiveLiftEdit> prior_edits;
                            for (Materialization const &materialization : materializations_.materializations_so_far())
                                prior_edits.push_back(
                                    {materialization.range.begin, materialization.range.end, materialization.text});
                            options_.live_lift->observe(
                                metadata.str(), model_context, model, std::move(prior_edits),
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
                selected = {model_selection.source,
                            "direct-proof-model",
                            std::nullopt,
                            std::nullopt,
                            {},
                            {},
                            model_selection.cost,
                            expected,
                            {}};
                selected.open = model_selection.source == "?";
                selected.complete = model_selection.complete;
                selected.closed_frontier = model_selection.closed_frontier;
                selected.open_leaves = model_selection.open_leaves;
                if (rainfall_)
                    rainfall_->record(
                        "derive", "proof.search.live.model", {"run:" + run}, "fine.proof-model-selector",
                        "One directly constructed bounded grammar produced the next exact source checkpoint while "
                        "the following epoch may continue independently of presentation",
                        {RainfallRecorder::string_field("hole", name),
                         RainfallRecorder::string_field("body", selected.source),
                         RainfallRecorder::number_field("budget", budget),
                         RainfallRecorder::number_field("cost", selected.cost),
                         RainfallRecorder::boolean_field("complete", selected.complete),
                         RainfallRecorder::number_field("closed_frontier", selected.closed_frontier),
                         RainfallRecorder::number_field("open_leaves", selected.open_leaves),
                         RainfallRecorder::number_field("grammar_productions", grammar.productions.size()),
                         RainfallRecorder::number_field("grammar_states", model_selection.state_count),
                         RainfallRecorder::number_field("grammar_transitions", model_selection.transition_count),
                         RainfallRecorder::number_field("grammar_states_reused", model_selection.reused_state_count),
                         RainfallRecorder::boolean_field("grammar_reset", model_selection.state_grammar_reset),
                         RainfallRecorder::boolean_field("candidate_trees_enumerated", false)});
                if (selected.complete ||
                    (options_.live_proof_search_limit != 0 && budget >= options_.live_proof_search_limit))
                    break;
            }

            materializations_.request_materialization(syntax::ConcreteRange::from_span(expression.span),
                                                      selected.source, expression.span);
            if (selected.complete)
                ++holes_filled_;
            else {
                ++holes_checkpointed_;
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

        std::vector<ProofCandidate> candidates;
        std::optional<proof_model::Grammar> model_grammar;
        std::optional<proof_model::Result> model_selection;
        if (options_.proof_selector == ProofSelector::z3_model) {
            model_grammar = make_direct_proof_model_grammar(
                "direct-hole-" + std::to_string(proof_model_index_++), expected, print_value(expected_syntax.left),
                print_value(expected_syntax.right), proofs, proof_order, options_.proof_search_cost);
            model_grammar->retain_state_graph = rainfall_ != nullptr;
            model_selection = proof_model::select(values_.context(), *model_grammar);
            if (model_selection->status != proof_model::Status::sat)
                reject(expression.span,
                       "Z3 proof model selector failed for `" + name + "`: " + model_selection->reason);
            if (model_selection->root_production >= model_grammar->productions.size())
                throw std::logic_error("proof model selected an absent root production");
            auto const &root = model_grammar->productions[model_selection->root_production];
            ProofCandidate selected{model_selection->source,
                                    proof_model_kind(root.kind),
                                    std::nullopt,
                                    std::nullopt,
                                    root.index_arguments,
                                    model_selection->root_children,
                                    model_selection->cost,
                                    expected,
                                    {}};
            selected.complete = model_selection->complete;
            selected.closed_frontier = model_selection->closed_frontier;
            selected.open_leaves = model_selection->open_leaves;
            selected.open = root.kind == proof_model::ProductionKind::open;
            if (root.kind == proof_model::ProductionKind::local)
                selected.local_proof = root.source;
            if (root.kind == proof_model::ProductionKind::application)
                selected.proof_function = root.function;
            candidates.push_back(std::move(selected));
        }
        else {
            candidates = enumerate_proof_candidates(expected_syntax, expected, print_value(expected_syntax.left),
                                                    print_value(expected_syntax.right), values, proofs, proof_order,
                                                    absorbed, options_.proof_search_cost);
        }

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
                 RainfallRecorder::boolean_field("candidate_trees_enumerated",
                                                 options_.proof_selector != ProofSelector::z3_model),
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
                    RainfallRecorder::string_field(
                        "origin", options_.proof_selector == ProofSelector::z3_model ? "model-lift" : "enumeration"),
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
        if (options_.proof_selector == ProofSelector::z3_model)
            selected_index = 0;

        ProofCandidate const &selected = candidates[selected_index];
        materializations_.request_materialization(syntax::ConcreteRange::from_span(expression.span), selected.source,
                                                  expression.span);
        if (selected.complete)
            ++holes_filled_;
        else
            ++holes_checkpointed_;
        if (rainfall_) {
            if (model_grammar && model_selection) {
                std::string grammar_event = rainfall_->record(
                    "object", "proof.model.grammar", {"run:" + run, hole}, "fine.proof-model-selector",
                    "Fine discovers typed productions directly and retains the complete bounded state graph; no "
                    "candidate tree frontier is constructed before Z3 selection",
                    {RainfallRecorder::string_field("hole", hole),
                     RainfallRecorder::string_field("grammar", model_grammar->id),
                     RainfallRecorder::raw_field("expected", proof_model_type_json(model_grammar->expected)),
                     RainfallRecorder::number_field("max_cost", model_grammar->max_cost),
                     RainfallRecorder::boolean_field("preferred_complete", model_selection->complete),
                     RainfallRecorder::number_field("preferred_closed_frontier", model_selection->closed_frontier),
                     RainfallRecorder::number_field("preferred_open_leaves", model_selection->open_leaves),
                     RainfallRecorder::number_field("preferred_cost", model_selection->cost),
                     RainfallRecorder::string_field("preferred_source", model_selection->source),
                     RainfallRecorder::string_field("root_state", model_selection->root_state),
                     RainfallRecorder::number_field("selected_root_production", model_selection->root_production),
                     RainfallRecorder::number_field("states", model_selection->state_count),
                     RainfallRecorder::number_field("transitions", model_selection->transition_count),
                     RainfallRecorder::raw_field("productions", proof_model_productions_json(*model_grammar)),
                     RainfallRecorder::raw_field("state_graph", proof_model_states_json(model_selection->state_graph)),
                     RainfallRecorder::string_field("selected_candidate", candidate_events[selected_index]),
                     RainfallRecorder::boolean_field("candidate_trees_enumerated", false)});
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
                                   RainfallRecorder::boolean_field("in_bounded_grammar", true),
                                   RainfallRecorder::boolean_field("candidate_trees_enumerated", false),
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
                 RainfallRecorder::string_field("residual_grammar", model_grammar ? model_grammar->id : ""),
                 RainfallRecorder::boolean_field("candidate_trees_enumerated",
                                                 options_.proof_selector != ProofSelector::z3_model),
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

}  // namespace fine::elaboration
