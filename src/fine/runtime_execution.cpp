#include "runtime_internal.h"

// Document execution: declaration ordering, value functions and their
// coeffects, caller-side calls, run scopes, proofs, and assertions.
namespace fine::runtime_detail {

    Elaborator::Elaborator(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                           std::string rainfall_run, ExecutionOptions options)
        : output_(output), options_(options) {
        if (rainfall_output)
            rainfall_.emplace(context_, *rainfall_output, std::move(rainfall_run), snapshot);
    }
    ExecutionResult Elaborator::execute(syntax::Document const &document) {
        for (auto const &enumeration : document.enums)
            declare_enum(enumeration);
        record_boundary();
        for (auto const &family : document.proof_inductives)
            declare_proof_inductive(family);
        for (auto const &function : document.proof_functions)
            declare_proof_function(function);
        for (auto const &function : document.functions)
            declare_function(function);
        if (document.run)
            execute_run(*document.run);
        for (auto const &[range, text] : materializations_)
            result_.materializations.push_back({{range.first, range.second}, text});
        if (rainfall_) {
            rainfall_->validate_terms();
            std::vector<std::string> dependencies;
            if (document.run)
                dependencies.push_back("run:" + document.run->name);
            rainfall_->record(
                "transition", document.run ? "proof-core.run.close" : "proof-core.document.close",
                std::move(dependencies), "fine.two-level-elaborator",
                result_.checkpoint_open
                    ? "A typed partial proof was checked through its fixed subtree; unresolved leaves remain holes"
                : document.run ? "All value terms checked; proof evidence remained virtual and every coeffect resolved"
                               : "All declarations checked without manufacturing an empty executable run",
                {RainfallRecorder::string_field("status", result_.checkpoint_open ? "checkpointed" : "verified"),
                 RainfallRecorder::number_field("functions_verified", result_.functions_verified),
                 RainfallRecorder::number_field("proof_functions_verified", result_.proof_functions_verified),
                 RainfallRecorder::number_field("proofs_formed", result_.proofs_formed),
                 RainfallRecorder::number_field("proof_holes_filled", result_.proof_holes_filled),
                 RainfallRecorder::number_field("proof_holes_checkpointed", result_.proof_holes_checkpointed),
                 RainfallRecorder::number_field("coeffects_resolved", result_.coeffects_resolved),
                 RainfallRecorder::number_field("runtime_proof_values", 0)});
        }
        if (document.run)
            output_ << (result_.checkpoint_open ? "checkpointed run: " : "verified run: ") << document.run->name
                    << '\n';
        else
            output_ << (result_.checkpoint_open ? "checkpointed definitions\n" : "verified definitions\n");
        output_ << "runtime-value-kinds: Int, Bool";
        for (auto const &[name, enumeration] : enums_)
            output_ << ", " << name;
        output_ << '\n' << "runtime-proof-values: 0 (unrepresentable)\n";
        return result_;
    }
    void Elaborator::declare_function(syntax::FunctionDecl const &declaration) {
        if (functions_.contains(declaration.name) || proof_functions_.contains(declaration.name) ||
            constructors_.contains(declaration.name))
            reject(declaration.span, "duplicate function `" + declaration.name + "`");
        ValueEnvironment values;
        ProofEnvironment proofs;
        std::vector<std::string> proof_order;
        std::vector<z3::expr> absorbed;
        std::set<std::string> names;
        for (auto const &parameter : declaration.parameters) {
            if (!names.insert(parameter.name).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            require_known_type(parameter.type);
            ValueKind kind = kind_of(parameter.type);
            std::string symbol = "fine." + declaration.name + "." + parameter.name;
            values.emplace(parameter.name, ValueTerm(kind, context_.constant(symbol.c_str(), sort(kind))));
        }
        for (auto const &coeffect : declaration.coeffects) {
            if (!names.insert(coeffect.name).second)
                reject(coeffect.span, "duplicate parameter `" + coeffect.name + "`");
            IdentityType type = elaborate_identity(coeffect.type, values, proofs, proof_order, absorbed);
            ProofEvidence evidence(coeffect.name, std::move(type), "coeffect", coeffect.span,
                                   print_value(coeffect.type.left), print_value(coeffect.type.right));
            std::string source;
            if (rainfall_) {
                source = rainfall_->source_node(coeffect.type.node_id, coeffect.type.span, "proof-type.identity");
                IdentityType const &identity = std::get<IdentityType>(evidence.type);
                std::string proposition = rainfall_->term(identity.left == identity.right, "coeffect-proposition");
                rainfall_->record("object", "coeffect.demand.declare", {"function:" + declaration.name},
                                  "fine.two-level-elaborator",
                                  "Function signature declares identity evidence required from each caller",
                                  {RainfallRecorder::string_field("function", declaration.name),
                                   RainfallRecorder::string_field("coeffect", coeffect.name),
                                   RainfallRecorder::string_field("proof_type", print_identity(coeffect.type)),
                                   RainfallRecorder::string_field("source", source),
                                   RainfallRecorder::string_field("proposition", proposition)});
            }
            auto [found, inserted] = proofs.emplace(coeffect.name, std::move(evidence));
            proof_order.push_back(coeffect.name);
            absorb(found->second, absorbed, {"function:" + declaration.name}, "hypothetical-coeffect",
                   source.empty() ? std::nullopt : std::optional(source));
        }
        ValueTerm body = elaborate_value(declaration.body, values, proofs, proof_order, absorbed);
        require_known_type(declaration.result_type);
        ValueKind result_kind = kind_of(declaration.result_type);
        if (body.kind != result_kind)
            reject(declaration.body.span,
                   "function body does not have declared result type `" + std::string(kind_name(result_kind)) + "`");
        values.emplace("result", body);
        std::vector<z3::expr> ensures;
        for (auto const &clause : declaration.ensures) {
            ValueTerm proposition = elaborate_value(clause, values, proofs, proof_order, absorbed);
            if (proposition.kind != boolean_kind())
                reject(clause.span, "function guarantee is not Bool");
            ensures.push_back(proposition.expression);
        }
        z3::solver solver(context_);
        for (auto const &assumption : absorbed)
            solver.add(assumption);
        if (!ensures.empty()) {
            z3::expr guarantee = ensures.front();
            for (std::size_t i = 1; i < ensures.size(); ++i)
                guarantee = guarantee && ensures[i];
            solver.add(!guarantee);
            if (solver.check() != z3::unsat)
                reject(declaration.span,
                       "function `" + declaration.name + "` does not satisfy its guarantees under declared coeffects");
        }
        functions_.emplace(declaration.name, &declaration);
        ++result_.functions_verified;
        output_ << "verified function: " << declaration.name << '\n';
        if (rainfall_)
            rainfall_->record("transition", "function.verify", {"function:" + declaration.name},
                              "fine.two-level-elaborator",
                              "Function body and guarantees checked under absorbed declared coeffects",
                              {RainfallRecorder::string_field("function", declaration.name),
                               RainfallRecorder::string_field("status", "unsat"),
                               RainfallRecorder::number_field("coeffects", declaration.coeffects.size()),
                               RainfallRecorder::number_field("guarantees", declaration.ensures.size())});
    }
    ValueTerm Elaborator::elaborate_call(syntax::ValueExpr const &expression, ValueEnvironment const &caller_values,
                                         ProofEnvironment const &caller_proofs,
                                         std::vector<std::string> const &caller_proof_order,
                                         std::vector<z3::expr> const &caller_absorbed) {
        auto found = functions_.find(expression.name);
        if (found == functions_.end()) {
            if (proof_functions_.contains(expression.name))
                reject(expression.span,
                       "proof function `" + expression.name + "` cannot be called from a runtime value expression");
            if (proof_constructors_.contains(expression.name))
                reject(expression.span,
                       "proof constructor `" + expression.name + "` cannot be called from a runtime value expression");
            reject(expression.span, "unknown function `" + expression.name + "`");
        }
        syntax::FunctionDecl const &function = *found->second;
        if (expression.elements.size() != function.parameters.size())
            reject(expression.span, "function `" + expression.name + "` expects " +
                                        std::to_string(function.parameters.size()) + " value arguments");
        ValueEnvironment callee_values;
        ProofEnvironment callee_proofs;
        std::vector<std::string> callee_proof_order;
        std::vector<z3::expr> callee_absorbed;
        for (std::size_t i = 0; i < expression.elements.size(); ++i) {
            ValueTerm argument = elaborate_value(expression.elements[i], caller_values, caller_proofs,
                                                 caller_proof_order, caller_absorbed);
            require_known_type(function.parameters[i].type);
            ValueKind expected = kind_of(function.parameters[i].type);
            if (argument.kind != expected)
                reject(expression.elements[i].span,
                       "argument `" + function.parameters[i].name + "` has the wrong value type");
            callee_values.emplace(function.parameters[i].name, std::move(argument));
        }
        std::map<std::string, std::string> explicit_arguments;
        for (auto const &argument : expression.using_proofs) {
            if (!explicit_arguments.emplace(argument.coeffect, argument.proof).second)
                reject(argument.span, "duplicate explicit coeffect `" + argument.coeffect + "`");
        }
        std::vector<std::pair<std::string, std::string>> chosen;
        for (auto const &coeffect : function.coeffects) {
            IdentityType demand =
                elaborate_identity(coeffect.type, callee_values, callee_proofs, callee_proof_order, callee_absorbed);
            std::string proof_name;
            bool explicit_choice = false;
            if (auto explicit_found = explicit_arguments.find(coeffect.name);
                explicit_found != explicit_arguments.end()) {
                proof_name = explicit_found->second;
                explicit_choice = true;
                explicit_arguments.erase(explicit_found);
            }
            else {
                if (options_.require_explicit_coeffects)
                    reject(expression.span, "implicit coeffect `" + expression.name + "." + coeffect.name +
                                                "` remains after materialization");
                for (auto const &candidate : caller_proof_order) {
                    auto candidate_found = caller_proofs.find(candidate);
                    if (candidate_found != caller_proofs.end() &&
                        std::holds_alternative<IdentityType>(candidate_found->second.type) &&
                        same_type(context_, std::get<IdentityType>(candidate_found->second.type), demand)) {
                        proof_name = candidate;
                        break;
                    }
                }
                if (proof_name.empty())
                    reject(expression.span, "missing caller proof for coeffect `" + expression.name + "." +
                                                coeffect.name + " : " + print_identity(coeffect.type) + "`");
            }
            auto evidence_found = caller_proofs.find(proof_name);
            if (evidence_found == caller_proofs.end())
                reject(expression.span, "unknown caller proof `" + proof_name + "`");
            if (!std::holds_alternative<IdentityType>(evidence_found->second.type) ||
                !same_type(context_, std::get<IdentityType>(evidence_found->second.type), demand))
                reject(expression.span, "caller proof `" + proof_name + "` does not satisfy coeffect `" +
                                            expression.name + "." + coeffect.name + "`");
            ProofEvidence supplied(coeffect.name, std::move(demand), "caller:" + proof_name,
                                   evidence_found->second.span, evidence_found->second.left_source,
                                   evidence_found->second.right_source);
            auto [inserted, ok] = callee_proofs.emplace(coeffect.name, std::move(supplied));
            callee_proof_order.push_back(coeffect.name);
            absorb(inserted->second, callee_absorbed, {"call:" + expression.name}, "resolved-coeffect");
            chosen.emplace_back(coeffect.name, proof_name);
            ++result_.coeffects_resolved;
            output_ << "resolved coeffect: " << expression.name << '.' << coeffect.name << " <- " << proof_name
                    << (explicit_choice ? " (explicit)" : " (lexical search)") << '\n';
            if (rainfall_) {
                IdentityType const &identity = std::get<IdentityType>(inserted->second.type);
                std::string demand_term =
                    rainfall_->term(identity.left == identity.right, "instantiated-coeffect-proposition");
                rainfall_->record("derive", "coeffect.demand.instantiate", {"call:" + expression.name},
                                  "fine.two-level-elaborator",
                                  "Value arguments instantiate the callee's identity demand in the caller's manager",
                                  {RainfallRecorder::string_field("function", expression.name),
                                   RainfallRecorder::string_field("coeffect", coeffect.name),
                                   RainfallRecorder::string_field("proposition", demand_term)});
                rainfall_->record(
                    "derive", "coeffect.resolve", {"call:" + expression.name}, "fine.lexical-proof-search",
                    "Exact caller-local identity evidence selected; no global instance search",
                    {RainfallRecorder::string_field("function", expression.name),
                     RainfallRecorder::string_field("coeffect", coeffect.name),
                     RainfallRecorder::string_field("proof", proof_name),
                     RainfallRecorder::string_field("mode", explicit_choice ? "explicit" : "exact-local")});
                rainfall_->record(
                    "derive", "coeffect.use", {"call:" + expression.name}, "fine.proof-context",
                    "Resolved proof is supplied virtually and only its proposition enters callee checking",
                    {RainfallRecorder::string_field("function", expression.name),
                     RainfallRecorder::string_field("coeffect", coeffect.name),
                     RainfallRecorder::string_field("proof", proof_name),
                     RainfallRecorder::boolean_field("runtime_argument_created", false)});
            }
        }
        if (!explicit_arguments.empty())
            reject(expression.span, "call supplies unknown coeffect `" + explicit_arguments.begin()->first + "`");
        if (expression.using_proofs.empty() && !chosen.empty()) {
            std::ostringstream insertion;
            insertion << " using [";
            for (std::size_t i = 0; i < chosen.size(); ++i) {
                if (i)
                    insertion << ", ";
                insertion << chosen[i].first << " = " << chosen[i].second;
            }
            insertion << ']';
            request_materialization(syntax::ConcreteRange::empty_at(expression.call_argument_end), insertion.str(),
                                    expression.span);
        }
        ValueTerm result =
            elaborate_value(function.body, callee_values, callee_proofs, callee_proof_order, callee_absorbed);
        require_known_type(function.result_type);
        if (result.kind != kind_of(function.result_type))
            reject(expression.span, "internal function result type mismatch");
        return result;
    }
    void Elaborator::execute_run(syntax::RunDecl const &run) {
        ValueEnvironment values;
        ProofEnvironment proofs;
        std::vector<std::string> proof_order;
        std::vector<z3::expr> absorbed;
        std::size_t assertion_index = 0;
        for (auto const &statement : run.statements) {
            std::visit(
                [&](auto const &item) {
                    execute_statement(item, run.name, assertion_index, values, proofs, proof_order, absorbed);
                },
                statement);
            if (result_.checkpoint_open)
                break;
        }
    }
    void Elaborator::execute_statement(syntax::LetDecl const &declaration, std::string const &run, std::size_t &,
                                       ValueEnvironment &values, ProofEnvironment &proofs,
                                       std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed) {
        ensure_fresh(declaration.name, declaration.span, values, proofs);
        ValueTerm value = elaborate_value(declaration.value, values, proofs, proof_order, absorbed);
        require_known_type(declaration.type);
        ValueKind expected = kind_of(declaration.type);
        if (value.kind != expected)
            reject(declaration.span, "value binding `" + declaration.name + "` has the wrong type");
        if (rainfall_)
            rainfall_->source_term(declaration.value.node_id, declaration.value.span, "value.expression",
                                   value.expression, "exact", {"run:" + run});
        values.emplace(declaration.name, std::move(value));
    }
    void Elaborator::execute_statement(syntax::ProofDecl const &declaration, std::string const &run, std::size_t &,
                                       ValueEnvironment &values, ProofEnvironment &proofs,
                                       std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed) {
        ensure_fresh(declaration.name, declaration.span, values, proofs);
        SemanticProofType expected = elaborate_proof_type(declaration.type, values, proofs, proof_order, absorbed);
        std::string proof_source;
        std::string type_source;
        if (rainfall_) {
            proof_source = rainfall_->source_node(
                declaration.value.node_id, declaration.value.span,
                declaration.value.kind == syntax::ProofExpr::Kind::hole ? "proof.expression.hole" : "proof.expression");
            type_source = rainfall_->source_node(declaration.type.node_id, declaration.type.span,
                                                 declaration.type.kind == syntax::ProofType::Kind::identity
                                                     ? "proof-type.identity"
                                                     : "proof-type.inductive");
        }
        ProofEvidence evidence =
            elaborate_any_proof(declaration.value, declaration.type, std::move(expected), values, proofs, proof_order,
                                absorbed, declaration.name, run, proof_source, type_source);
        if (!evidence.complete) {
            if (!options_.synthesize_partial_proofs && !options_.validate_partial_proofs)
                throw std::logic_error("open proof escaped checkpoint mode");
            result_.checkpoint_open = true;
            output_ << "retained typed proof holes: " << declaration.name << " : " << print_proof_type(declaration.type)
                    << '\n';
            return;
        }
        if (rainfall_) {
            if (auto identity = std::get_if<IdentityType>(&evidence.type)) {
                std::string proposition = rainfall_->term(identity->left == identity->right, "identity-proposition");
                rainfall_->record("object", "proof.identity.form", {"run:" + run}, "fine.proof-elaborator",
                                  "Exact source proof evidence formed at the static level; no runtime term exists",
                                  {RainfallRecorder::string_field("proof", declaration.name),
                                   RainfallRecorder::string_field("formation", evidence.formation),
                                   RainfallRecorder::string_field("proof_source", proof_source),
                                   RainfallRecorder::string_field("type_source", type_source),
                                   RainfallRecorder::string_field("proof_type", print_identity(declaration.type)),
                                   RainfallRecorder::string_field("proposition", proposition),
                                   RainfallRecorder::boolean_field("runtime_value_created", false)});
            }
            else {
                rainfall_->record("object", "proof.inductive.form", {"run:" + run}, "fine.proof-elaborator",
                                  "Indexed constructor evidence formed only at the static proof level",
                                  {RainfallRecorder::string_field("proof", declaration.name),
                                   RainfallRecorder::string_field("formation", evidence.formation),
                                   RainfallRecorder::string_field("proof_source", proof_source),
                                   RainfallRecorder::string_field("type_source", type_source),
                                   RainfallRecorder::string_field("proof_type", print_proof_type(declaration.type)),
                                   RainfallRecorder::boolean_field("runtime_value_created", false)});
            }
        }
        auto [inserted, ok] = proofs.emplace(declaration.name, std::move(evidence));
        proof_order.push_back(declaration.name);
        absorb(inserted->second, absorbed, {"run:" + run}, "local-proof",
               proof_source.empty() ? std::nullopt : std::optional(proof_source));
        ++result_.proofs_formed;
        output_ << "formed proof: " << declaration.name << " : " << print_proof_type(declaration.type)
                << " (virtual)\n";
    }
    void Elaborator::execute_statement(syntax::AssertDecl const &declaration, std::string const &run,
                                       std::size_t &assertion_index, ValueEnvironment &values, ProofEnvironment &proofs,
                                       std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed) {
        ValueTerm proposition = elaborate_value(declaration.proposition, values, proofs, proof_order, absorbed);
        if (proposition.kind != boolean_kind())
            reject(declaration.span, "assertion is not Bool");
        z3::solver solver(context_);
        for (auto const &assumption : absorbed)
            solver.add(assumption);
        solver.add(!proposition.expression);
        if (solver.check() != z3::unsat)
            reject(declaration.span, "assertion may be false");
        output_ << "verified assertion: " << run << '.' << assertion_index++ << '\n';
        if (rainfall_) {
            rainfall_->source_term(declaration.proposition.node_id, declaration.proposition.span, "value.assertion",
                                   proposition.expression, "exact", {"run:" + run});
            rainfall_->record("transition", "assert.verify", {"run:" + run}, "fine.two-level-elaborator",
                              "Assertion refuted under all previously absorbed identity propositions",
                              {RainfallRecorder::string_field("status", "unsat")});
        }
    }

}  // namespace fine::runtime_detail
