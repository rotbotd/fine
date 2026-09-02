#include "runtime.h"

#include "rainfall.h"

#include "c++/z3++.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace fine {
    namespace {

        enum class ValueKind { integer, boolean };

        char const *kind_name(ValueKind kind) {
            return kind == ValueKind::integer ? "Int" : "Bool";
        }

        ValueKind kind_of(syntax::ValueType const &type) {
            return type.kind == syntax::ValueType::Kind::integer ? ValueKind::integer : ValueKind::boolean;
        }

        struct ValueTerm {
            ValueKind kind;
            z3::expr expression;

            ValueTerm(ValueKind kind, z3::expr expression) : kind(kind), expression(std::move(expression)) {}
        };

        // ProofEvidence is deliberately not a ValueTerm case. It has no runtime
        // payload: its only semantic projection is the proposition absorbed by
        // the elaborator into an SMT context.
        struct IdentityType {
            ValueKind carrier;
            z3::expr left;
            z3::expr right;

            IdentityType(ValueKind carrier, z3::expr left, z3::expr right)
                : carrier(carrier), left(std::move(left)), right(std::move(right)) {}
        };

        struct ProofEvidence {
            std::string name;
            IdentityType type;
            std::string formation;
            syntax::SourceSpan span;

            ProofEvidence(std::string name, IdentityType type, std::string formation,
                          syntax::SourceSpan span)
                : name(std::move(name)), type(std::move(type)), formation(std::move(formation)), span(span) {}
        };

        using ValueEnvironment = std::map<std::string, ValueTerm>;
        using ProofEnvironment = std::map<std::string, ProofEvidence>;

        bool same_ast(z3::context &context, z3::expr const &left, z3::expr const &right) {
            return Z3_is_eq_ast(context, left, right);
        }

        bool same_type(z3::context &context, IdentityType const &left, IdentityType const &right) {
            return left.carrier == right.carrier && same_ast(context, left.left, right.left) &&
                   same_ast(context, left.right, right.right);
        }

        std::string print_value(syntax::ValueExpr const &expression) {
            switch (expression.kind) {
            case syntax::ValueExpr::Kind::name:
                return expression.name;
            case syntax::ValueExpr::Kind::integer:
                return expression.integer_text;
            case syntax::ValueExpr::Kind::boolean:
                return expression.boolean_value ? "true" : "false";
            case syntax::ValueExpr::Kind::equal:
                return print_value(expression.elements[0]) + " == " + print_value(expression.elements[1]);
            case syntax::ValueExpr::Kind::call: {
                std::ostringstream result;
                result << expression.name << '(';
                for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                    if (i)
                        result << ", ";
                    result << print_value(expression.elements[i]);
                }
                result << ')';
                if (!expression.using_proofs.empty()) {
                    result << " using [";
                    for (std::size_t i = 0; i < expression.using_proofs.size(); ++i) {
                        if (i)
                            result << ", ";
                        result << expression.using_proofs[i].coeffect << " = "
                               << expression.using_proofs[i].proof;
                    }
                    result << ']';
                }
                return result.str();
            }
            }
            return "<value>";
        }

        std::string print_identity(syntax::ProofType const &type) {
            std::ostringstream result;
            result << "Id(" << kind_name(kind_of(type.carrier)) << ", " << print_value(type.left)
                   << ", " << print_value(type.right) << ')';
            return result.str();
        }

        std::string source_line(std::string_view source, std::size_t line_number) {
            std::size_t current = 1;
            std::size_t begin = 0;
            while (current < line_number && begin < source.size()) {
                std::size_t next = source.find('\n', begin);
                if (next == std::string_view::npos)
                    return {};
                begin = next + 1;
                ++current;
            }
            std::size_t end = source.find('\n', begin);
            if (end == std::string_view::npos)
                end = source.size();
            return std::string(source.substr(begin, end - begin));
        }

        class Elaborator {
        public:
            Elaborator(std::ostream &output, std::ostream *rainfall_output,
                       SourceSnapshot const *snapshot, std::string rainfall_run,
                       ExecutionOptions options)
                : output_(output), options_(options) {
                if (rainfall_output)
                    rainfall_.emplace(context_, *rainfall_output, std::move(rainfall_run), snapshot);
            }

            ExecutionResult execute(syntax::Document const &document) {
                record_boundary();
                for (auto const &function : document.functions)
                    declare_function(function);
                execute_run(document.run);
                if (rainfall_) {
                    rainfall_->validate_terms();
                    rainfall_->record(
                        "transition", "proof-core.run.close", {"run:" + document.run.name},
                        "fine.two-level-elaborator",
                        "All value terms checked; proof evidence remained virtual and every coeffect resolved",
                        {RainfallRecorder::string_field("status", "verified"),
                         RainfallRecorder::number_field("functions_verified", result_.functions_verified),
                         RainfallRecorder::number_field("proofs_formed", result_.proofs_formed),
                         RainfallRecorder::number_field("coeffects_resolved", result_.coeffects_resolved),
                         RainfallRecorder::number_field("runtime_proof_values", 0)});
                }
                output_ << "verified run: " << document.run.name << '\n'
                        << "runtime-value-kinds: Int, Bool\n"
                        << "runtime-proof-values: 0 (unrepresentable)\n";
                return result_;
            }

        private:
            z3::context context_;
            std::ostream &output_;
            ExecutionOptions options_;
            std::optional<RainfallRecorder> rainfall_;
            std::map<std::string, syntax::FunctionDecl const *> functions_;
            ExecutionResult result_;
            std::map<std::size_t, std::string> materializations_;

            [[noreturn]] static void reject(syntax::SourceSpan span, std::string message) {
                throw SemanticError(span, std::move(message));
            }

            z3::sort sort(ValueKind kind) {
                return kind == ValueKind::integer ? context_.int_sort() : context_.bool_sort();
            }

            void record_boundary() {
                if (!rainfall_)
                    return;
                rainfall_->record(
                    "object", "proof.erasure.boundary", {}, "fine.two-level-core",
                    "The runtime value representation is closed over Int and Bool; ProofEvidence is a disjoint elaborator-only type",
                    {RainfallRecorder::raw_field("runtime_value_kinds", "[\"Int\",\"Bool\"]"),
                     RainfallRecorder::number_field("runtime_proof_variants", 0),
                     RainfallRecorder::boolean_field("proof_evidence_elaboration_only", true)});
            }

            void ensure_fresh(std::string const &name, syntax::SourceSpan span,
                              ValueEnvironment const &values, ProofEnvironment const &proofs) {
                if (values.contains(name) || proofs.contains(name))
                    reject(span, "duplicate local name `" + name + "`");
            }

            ValueTerm elaborate_value(syntax::ValueExpr const &expression,
                                      ValueEnvironment const &values,
                                      ProofEnvironment const &proofs,
                                      std::vector<std::string> const &proof_order,
                                      std::vector<z3::expr> const &absorbed) {
                switch (expression.kind) {
                case syntax::ValueExpr::Kind::name: {
                    auto value = values.find(expression.name);
                    if (value != values.end())
                        return value->second;
                    if (proofs.contains(expression.name))
                        reject(expression.span, "proof `" + expression.name +
                                                "` cannot inhabit a runtime value");
                    reject(expression.span, "unknown value `" + expression.name + "`");
                }
                case syntax::ValueExpr::Kind::integer:
                    return {ValueKind::integer, context_.int_val(expression.integer_text.c_str())};
                case syntax::ValueExpr::Kind::boolean:
                    return {ValueKind::boolean, context_.bool_val(expression.boolean_value)};
                case syntax::ValueExpr::Kind::equal: {
                    ValueTerm left = elaborate_value(expression.elements[0], values, proofs, proof_order, absorbed);
                    ValueTerm right = elaborate_value(expression.elements[1], values, proofs, proof_order, absorbed);
                    if (left.kind != right.kind)
                        reject(expression.span, "equality operands have different value types");
                    return {ValueKind::boolean, left.expression == right.expression};
                }
                case syntax::ValueExpr::Kind::call:
                    return elaborate_call(expression, values, proofs, proof_order, absorbed);
                }
                reject(expression.span, "unsupported value expression");
            }

            IdentityType elaborate_identity(syntax::ProofType const &type,
                                             ValueEnvironment const &values,
                                             ProofEnvironment const &proofs,
                                             std::vector<std::string> const &proof_order,
                                             std::vector<z3::expr> const &absorbed) {
                ValueKind carrier = kind_of(type.carrier);
                ValueTerm left = elaborate_value(type.left, values, proofs, proof_order, absorbed);
                ValueTerm right = elaborate_value(type.right, values, proofs, proof_order, absorbed);
                if (left.kind != carrier || right.kind != carrier)
                    reject(type.span, "identity endpoints do not have carrier type `" +
                                          std::string(kind_name(carrier)) + "`");
                return {carrier, std::move(left.expression), std::move(right.expression)};
            }

            ProofEvidence elaborate_proof(syntax::ProofExpr const &expression,
                                          IdentityType expected,
                                          ValueEnvironment const &values,
                                          ProofEnvironment const &proofs,
                                          std::vector<std::string> const &proof_order,
                                          std::vector<z3::expr> const &absorbed,
                                          std::string name) {
                if (expression.kind == syntax::ProofExpr::Kind::name) {
                    auto found = proofs.find(expression.name);
                    if (found == proofs.end())
                        reject(expression.span, "unknown proof `" + expression.name + "`");
                    if (!same_type(context_, found->second.type, expected))
                        reject(expression.span, "proof `" + expression.name + "` has the wrong identity type");
                    return {std::move(name), std::move(expected), "alias:" + expression.name, expression.span};
                }
                ValueTerm witness = elaborate_value(expression.value, values, proofs, proof_order, absorbed);
                if (witness.kind != expected.carrier ||
                    !same_ast(context_, witness.expression, expected.left) ||
                    !same_ast(context_, witness.expression, expected.right))
                    reject(expression.span, "`refl` requires both identity endpoints to elaborate to its exact value");
                return {std::move(name), std::move(expected), "refl", expression.span};
            }

            void absorb(ProofEvidence const &proof, std::vector<z3::expr> &absorbed,
                        std::vector<std::string> within, std::string_view role,
                        std::optional<std::string> source = std::nullopt) {
                z3::expr proposition = proof.type.left == proof.type.right;
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
                rainfall_->record(
                    "derive", "proof.context.absorb", within, "fine.proof-context",
                    "Identity evidence contributes its proposition to this lexical SMT context without becoming a runtime value",
                    data);
            }

            void declare_function(syntax::FunctionDecl const &declaration) {
                if (functions_.contains(declaration.name))
                    reject(declaration.span, "duplicate function `" + declaration.name + "`");
                ValueEnvironment values;
                ProofEnvironment proofs;
                std::vector<std::string> proof_order;
                std::vector<z3::expr> absorbed;
                std::set<std::string> names;
                for (auto const &parameter : declaration.parameters) {
                    if (!names.insert(parameter.name).second)
                        reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
                    ValueKind kind = kind_of(parameter.type);
                    std::string symbol = "fine." + declaration.name + "." + parameter.name;
                    values.emplace(parameter.name, ValueTerm(kind, context_.constant(symbol.c_str(), sort(kind))));
                }
                for (auto const &coeffect : declaration.coeffects) {
                    if (!names.insert(coeffect.name).second)
                        reject(coeffect.span, "duplicate parameter `" + coeffect.name + "`");
                    IdentityType type = elaborate_identity(coeffect.type, values, proofs, proof_order, absorbed);
                    ProofEvidence evidence(coeffect.name, std::move(type), "coeffect", coeffect.span);
                    std::string source;
                    if (rainfall_) {
                        source = rainfall_->source_node(coeffect.type.node_id, coeffect.type.span,
                                                       "proof-type.identity");
                        std::string proposition = rainfall_->term(evidence.type.left == evidence.type.right,
                                                                  "coeffect-proposition");
                        rainfall_->record(
                            "object", "coeffect.demand.declare", {"function:" + declaration.name},
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
                    absorb(found->second, absorbed, {"function:" + declaration.name},
                           "hypothetical-coeffect", source.empty() ? std::nullopt : std::optional(source));
                }
                ValueTerm body = elaborate_value(declaration.body, values, proofs, proof_order, absorbed);
                ValueKind result_kind = kind_of(declaration.result_type);
                if (body.kind != result_kind)
                    reject(declaration.body.span, "function body does not have declared result type `" +
                                                      std::string(kind_name(result_kind)) + "`");
                values.emplace("result", body);
                std::vector<z3::expr> ensures;
                for (auto const &clause : declaration.ensures) {
                    ValueTerm proposition = elaborate_value(clause, values, proofs, proof_order, absorbed);
                    if (proposition.kind != ValueKind::boolean)
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
                        reject(declaration.span, "function `" + declaration.name +
                                                     "` does not satisfy its guarantees under declared coeffects");
                }
                functions_.emplace(declaration.name, &declaration);
                ++result_.functions_verified;
                output_ << "verified function: " << declaration.name << '\n';
                if (rainfall_)
                    rainfall_->record(
                        "transition", "function.verify", {"function:" + declaration.name},
                        "fine.two-level-elaborator",
                        "Function body and guarantees checked under absorbed declared coeffects",
                        {RainfallRecorder::string_field("function", declaration.name),
                         RainfallRecorder::string_field("status", "unsat"),
                         RainfallRecorder::number_field("coeffects", declaration.coeffects.size()),
                         RainfallRecorder::number_field("guarantees", declaration.ensures.size())});
            }

            ValueTerm elaborate_call(syntax::ValueExpr const &expression,
                                     ValueEnvironment const &caller_values,
                                     ProofEnvironment const &caller_proofs,
                                     std::vector<std::string> const &caller_proof_order,
                                     std::vector<z3::expr> const &caller_absorbed) {
                auto found = functions_.find(expression.name);
                if (found == functions_.end())
                    reject(expression.span, "unknown function `" + expression.name + "`");
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
                    IdentityType demand = elaborate_identity(coeffect.type, callee_values, callee_proofs,
                                                             callee_proof_order, callee_absorbed);
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
                                same_type(context_, candidate_found->second.type, demand)) {
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
                    if (!same_type(context_, evidence_found->second.type, demand))
                        reject(expression.span, "caller proof `" + proof_name + "` does not satisfy coeffect `" +
                                                    expression.name + "." + coeffect.name + "`");
                    ProofEvidence supplied(coeffect.name, std::move(demand),
                                           "caller:" + proof_name, evidence_found->second.span);
                    auto [inserted, ok] = callee_proofs.emplace(coeffect.name, std::move(supplied));
                    callee_proof_order.push_back(coeffect.name);
                    absorb(inserted->second, callee_absorbed,
                           {"call:" + expression.name}, "resolved-coeffect");
                    chosen.emplace_back(coeffect.name, proof_name);
                    ++result_.coeffects_resolved;
                    output_ << "resolved coeffect: " << expression.name << '.' << coeffect.name
                            << " <- " << proof_name << (explicit_choice ? " (explicit)" : " (lexical search)") << '\n';
                    if (rainfall_) {
                        std::string demand_term = rainfall_->term(inserted->second.type.left == inserted->second.type.right,
                                                                 "instantiated-coeffect-proposition");
                        rainfall_->record(
                            "derive", "coeffect.demand.instantiate", {"call:" + expression.name},
                            "fine.two-level-elaborator",
                            "Value arguments instantiate the callee's identity demand in the caller's manager",
                            {RainfallRecorder::string_field("function", expression.name),
                             RainfallRecorder::string_field("coeffect", coeffect.name),
                             RainfallRecorder::string_field("proposition", demand_term)});
                        rainfall_->record(
                            "derive", "coeffect.resolve", {"call:" + expression.name},
                            "fine.lexical-proof-search",
                            "Exact caller-local identity evidence selected; no global instance search",
                            {RainfallRecorder::string_field("function", expression.name),
                             RainfallRecorder::string_field("coeffect", coeffect.name),
                             RainfallRecorder::string_field("proof", proof_name),
                             RainfallRecorder::string_field("mode", explicit_choice ? "explicit" : "exact-local")});
                        rainfall_->record(
                            "derive", "coeffect.use", {"call:" + expression.name},
                            "fine.proof-context",
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
                    auto [at, inserted] = materializations_.emplace(expression.call_argument_end, insertion.str());
                    if (!inserted && at->second != insertion.str())
                        reject(expression.span, "two coeffect materializations disagree at one source offset");
                }
                ValueTerm result = elaborate_value(function.body, callee_values, callee_proofs,
                                                   callee_proof_order, callee_absorbed);
                if (result.kind != kind_of(function.result_type))
                    reject(expression.span, "internal function result type mismatch");
                return result;
            }

            void execute_run(syntax::RunDecl const &run) {
                ValueEnvironment values;
                ProofEnvironment proofs;
                std::vector<std::string> proof_order;
                std::vector<z3::expr> absorbed;
                std::size_t assertion_index = 0;
                for (auto const &statement : run.statements) {
                    std::visit([&](auto const &item) { execute_statement(item, run.name, assertion_index,
                                                                  values, proofs, proof_order, absorbed); },
                               statement);
                }
                for (auto const &[offset, text] : materializations_)
                    result_.materializations.push_back({offset, text});
            }

            void execute_statement(syntax::LetDecl const &declaration, std::string const &run,
                                   std::size_t &, ValueEnvironment &values,
                                   ProofEnvironment &proofs, std::vector<std::string> &proof_order,
                                   std::vector<z3::expr> &absorbed) {
                ensure_fresh(declaration.name, declaration.span, values, proofs);
                ValueTerm value = elaborate_value(declaration.value, values, proofs, proof_order, absorbed);
                ValueKind expected = kind_of(declaration.type);
                if (value.kind != expected)
                    reject(declaration.span, "value binding `" + declaration.name + "` has the wrong type");
                if (rainfall_)
                    rainfall_->source_term(declaration.value.node_id, declaration.value.span, "value.expression",
                                           value.expression, "exact", {"run:" + run});
                values.emplace(declaration.name, std::move(value));
            }

            void execute_statement(syntax::ProofDecl const &declaration, std::string const &run,
                                   std::size_t &, ValueEnvironment &values,
                                   ProofEnvironment &proofs, std::vector<std::string> &proof_order,
                                   std::vector<z3::expr> &absorbed) {
                ensure_fresh(declaration.name, declaration.span, values, proofs);
                IdentityType expected = elaborate_identity(declaration.type, values, proofs, proof_order, absorbed);
                ProofEvidence evidence = elaborate_proof(declaration.value, std::move(expected), values,
                                                         proofs, proof_order, absorbed, declaration.name);
                std::string proof_source;
                std::string type_source;
                if (rainfall_) {
                    proof_source = rainfall_->source_node(declaration.value.node_id, declaration.value.span,
                                                          "proof.expression");
                    type_source = rainfall_->source_node(declaration.type.node_id, declaration.type.span,
                                                         "proof-type.identity");
                    std::string proposition = rainfall_->term(evidence.type.left == evidence.type.right,
                                                              "identity-proposition");
                    rainfall_->record(
                        "object", "proof.identity.form", {"run:" + run}, "fine.proof-elaborator",
                        "Exact source proof evidence formed at the static level; no runtime term exists",
                        {RainfallRecorder::string_field("proof", declaration.name),
                         RainfallRecorder::string_field("formation", evidence.formation),
                         RainfallRecorder::string_field("proof_source", proof_source),
                         RainfallRecorder::string_field("type_source", type_source),
                         RainfallRecorder::string_field("proof_type", print_identity(declaration.type)),
                         RainfallRecorder::string_field("proposition", proposition),
                         RainfallRecorder::boolean_field("runtime_value_created", false)});
                }
                auto [inserted, ok] = proofs.emplace(declaration.name, std::move(evidence));
                proof_order.push_back(declaration.name);
                absorb(inserted->second, absorbed, {"run:" + run}, "local-proof",
                       proof_source.empty() ? std::nullopt : std::optional(proof_source));
                ++result_.proofs_formed;
                output_ << "formed proof: " << declaration.name << " : " << print_identity(declaration.type)
                        << " (virtual)\n";
            }

            void execute_statement(syntax::AssertDecl const &declaration, std::string const &run,
                                   std::size_t &assertion_index, ValueEnvironment &values,
                                   ProofEnvironment &proofs, std::vector<std::string> &proof_order,
                                   std::vector<z3::expr> &absorbed) {
                ValueTerm proposition = elaborate_value(declaration.proposition, values, proofs,
                                                        proof_order, absorbed);
                if (proposition.kind != ValueKind::boolean)
                    reject(declaration.span, "assertion is not Bool");
                z3::solver solver(context_);
                for (auto const &assumption : absorbed)
                    solver.add(assumption);
                solver.add(!proposition.expression);
                if (solver.check() != z3::unsat)
                    reject(declaration.span, "assertion may be false");
                output_ << "verified assertion: " << run << '.' << assertion_index++ << '\n';
                if (rainfall_) {
                    rainfall_->source_term(declaration.proposition.node_id, declaration.proposition.span,
                                           "value.assertion", proposition.expression, "exact",
                                           {"run:" + run});
                    rainfall_->record(
                        "transition", "assert.verify", {"run:" + run}, "fine.two-level-elaborator",
                        "Assertion refuted under all previously absorbed identity propositions",
                        {RainfallRecorder::string_field("status", "unsat")});
                }
            }
        };

    }  // namespace

    SemanticError::SemanticError(syntax::SourceSpan span, std::string message)
        : std::runtime_error(std::move(message)), span_(span) {}

    std::string SemanticError::format(std::string_view filename, std::string_view source) const {
        std::ostringstream output;
        output << filename << ':' << span_.begin.line << ':' << span_.begin.column << ": " << what();
        std::string line = source_line(source, span_.begin.line);
        if (!line.empty()) {
            output << '\n' << line << '\n';
            std::size_t caret = span_.begin.column > 0 ? span_.begin.column - 1 : 0;
            output << std::string(caret, ' ') << '^';
        }
        return output.str();
    }

    ExecutionResult execute(syntax::Document const &document, std::ostream &output,
                            std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                            std::string rainfall_run, ExecutionOptions options) {
        return Elaborator(output, rainfall_output, snapshot, std::move(rainfall_run), options).execute(document);
    }

    std::string apply_materializations(std::string_view source,
                                       std::vector<Materialization> materializations) {
        std::sort(materializations.begin(), materializations.end(),
                  [](auto const &left, auto const &right) { return left.offset < right.offset; });
        std::ostringstream output;
        std::size_t cursor = 0;
        for (auto const &materialization : materializations) {
            if (materialization.offset < cursor || materialization.offset > source.size())
                throw std::runtime_error("invalid or overlapping coeffect materialization");
            output << source.substr(cursor, materialization.offset - cursor) << materialization.text;
            cursor = materialization.offset;
        }
        output << source.substr(cursor);
        return output.str();
    }

}  // namespace fine
