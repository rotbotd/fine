#include "runtime.h"

#include "proof_model_selector.h"
#include "rainfall.h"

#include "c++/z3++.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace fine {
    namespace {

        struct ValueKind {
            enum class Tag { integer, boolean, enumeration };

            Tag tag;
            std::string name;

            friend bool operator==(ValueKind const &, ValueKind const &) = default;
        };

        ValueKind integer_kind() {
            return {ValueKind::Tag::integer, "Int"};
        }

        ValueKind boolean_kind() {
            return {ValueKind::Tag::boolean, "Bool"};
        }

        std::string const &kind_name(ValueKind const &kind) {
            return kind.name;
        }

        ValueKind kind_of(syntax::ValueType const &type) {
            switch (type.kind) {
            case syntax::ValueType::Kind::integer: return integer_kind();
            case syntax::ValueType::Kind::boolean: return boolean_kind();
            case syntax::ValueType::Kind::enumeration: return {ValueKind::Tag::enumeration, type.name};
            }
            return integer_kind();
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

        struct InductiveType {
            std::string family;
            std::vector<ValueTerm> indices;
        };

        using SemanticProofType = std::variant<IdentityType, InductiveType>;

        struct ProofEvidence {
            std::string name;
            SemanticProofType type;
            std::string formation;
            syntax::SourceSpan span;
            std::string left_source;
            std::string right_source;
            std::vector<syntax::ValueExpr> index_syntax;
            // Present only for evidence bound from a recursive constructor field
            // below the named induction parameter. The parent remains distinct so
            // Rainfall can retain the exact descent edge.
            std::optional<std::string> structural_root;
            std::optional<std::string> structural_parent;

            ProofEvidence(std::string name, SemanticProofType type, std::string formation, syntax::SourceSpan span,
                          std::string left_source, std::string right_source,
                          std::vector<syntax::ValueExpr> index_syntax = {},
                          std::optional<std::string> structural_root = std::nullopt,
                          std::optional<std::string> structural_parent = std::nullopt)
                : name(std::move(name)), type(std::move(type)), formation(std::move(formation)), span(span),
                  left_source(std::move(left_source)), right_source(std::move(right_source)),
                  index_syntax(std::move(index_syntax)), structural_root(std::move(structural_root)),
                  structural_parent(std::move(structural_parent)) {}
        };

        using ValueEnvironment = std::map<std::string, ValueTerm>;
        using ProofEnvironment = std::map<std::string, ProofEvidence>;

        struct RuntimeConstructor {
            std::string name;
            std::vector<ValueKind> fields;
            z3::func_decl constructor;
            z3::func_decl tester;
            std::vector<z3::func_decl> accessors;

            RuntimeConstructor(z3::context &context, std::string name, std::vector<ValueKind> fields)
                : name(std::move(name)), fields(std::move(fields)), constructor(context), tester(context) {}
        };

        struct RuntimeEnum {
            ValueKind kind;
            z3::sort sort;
            std::vector<RuntimeConstructor> constructors;

            RuntimeEnum(z3::context &context, ValueKind kind) : kind(std::move(kind)), sort(context) {}
        };

        struct ProofCandidate {
            std::string source;
            std::string production;
            std::optional<std::string> local_proof;
            std::optional<std::string> proof_function;
            std::vector<std::string> index_arguments;
            std::vector<std::string> proof_arguments;
            std::size_t cost = 1;
            std::optional<IdentityType> type;
            std::vector<ProofCandidate> children;
        };

        struct IndexInstantiation {
            ValueEnvironment values;
            std::map<std::string, std::string> sources;
        };

        constexpr std::size_t max_proof_search_cost = 3;

        bool same_ast(z3::context &context, z3::expr const &left, z3::expr const &right) {
            return Z3_is_eq_ast(context, left, right);
        }

        bool same_type(z3::context &context, IdentityType const &left, IdentityType const &right) {
            return left.carrier == right.carrier && same_ast(context, left.left, right.left) &&
                   same_ast(context, left.right, right.right);
        }

        bool same_type(z3::context &context, InductiveType const &left, InductiveType const &right) {
            if (left.family != right.family || left.indices.size() != right.indices.size())
                return false;
            for (std::size_t i = 0; i < left.indices.size(); ++i)
                if (left.indices[i].kind != right.indices[i].kind ||
                    !same_ast(context, left.indices[i].expression, right.indices[i].expression))
                    return false;
            return true;
        }

        bool same_type(z3::context &context, SemanticProofType const &left, SemanticProofType const &right) {
            if (left.index() != right.index())
                return false;
            if (auto identity = std::get_if<IdentityType>(&left))
                return same_type(context, *identity, std::get<IdentityType>(right));
            return same_type(context, std::get<InductiveType>(left), std::get<InductiveType>(right));
        }

        std::string print_value(syntax::ValueExpr const &expression) {
            switch (expression.kind) {
            case syntax::ValueExpr::Kind::name: return expression.name;
            case syntax::ValueExpr::Kind::integer: return expression.integer_text;
            case syntax::ValueExpr::Kind::boolean: return expression.boolean_value ? "true" : "false";
            case syntax::ValueExpr::Kind::equal: {
                std::string left = print_value(expression.elements[0]);
                std::string right = print_value(expression.elements[1]);
                if (expression.elements[0].kind == syntax::ValueExpr::Kind::equal)
                    left = "(" + left + ")";
                if (expression.elements[1].kind == syntax::ValueExpr::Kind::equal)
                    right = "(" + right + ")";
                return left + " == " + right;
            }
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
                        result << expression.using_proofs[i].coeffect << " = " << expression.using_proofs[i].proof;
                    }
                    result << ']';
                }
                return result.str();
            }
            case syntax::ValueExpr::Kind::match: {
                std::ostringstream result;
                result << "match " << print_value(expression.elements[0]) << " { ";
                for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
                    if (i)
                        result << ", ";
                    result << expression.match_constructors[i];
                    if (!expression.match_binders[i].empty()) {
                        result << '(';
                        for (std::size_t j = 0; j < expression.match_binders[i].size(); ++j) {
                            if (j)
                                result << ", ";
                            result << expression.match_binders[i][j];
                        }
                        result << ')';
                    }
                    result << " => " << print_value(expression.elements[i + 1]);
                }
                return result.str() + " }";
            }
            }
            return "<value>";
        }

        std::string print_identity(syntax::ProofType const &type) {
            std::ostringstream result;
            result << "Id(" << kind_name(kind_of(type.carrier)) << ", " << print_value(type.left) << ", "
                   << print_value(type.right) << ')';
            return result.str();
        }

        std::string print_proof_type(syntax::ProofType const &type) {
            if (type.kind == syntax::ProofType::Kind::identity)
                return print_identity(type);
            std::ostringstream result;
            result << type.name << '(';
            for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                if (i)
                    result << ", ";
                result << print_value(type.arguments[i]);
            }
            return result.str() + ')';
        }

        std::string print_proof(syntax::ProofExpr const &expression) {
            switch (expression.kind) {
            case syntax::ProofExpr::Kind::name: return expression.name;
            case syntax::ProofExpr::Kind::reflexivity: return "refl(" + print_value(expression.value) + ")";
            case syntax::ProofExpr::Kind::application: {
                std::ostringstream result;
                result << expression.name;
                if (!expression.value_arguments.empty()) {
                    result << '[';
                    for (std::size_t i = 0; i < expression.value_arguments.size(); ++i) {
                        if (i)
                            result << ", ";
                        result << print_value(expression.value_arguments[i]);
                    }
                    result << ']';
                }
                result << '(';
                for (std::size_t i = 0; i < expression.proof_arguments.size(); ++i) {
                    if (i)
                        result << ", ";
                    result << print_proof(expression.proof_arguments[i]);
                }
                result << ')';
                return result.str();
            }
            case syntax::ProofExpr::Kind::match: {
                std::ostringstream result;
                result << "match " << expression.matched_proof << " { ";
                for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
                    if (i)
                        result << ", ";
                    result << expression.match_constructors[i];
                    if (!expression.match_value_binders[i].empty()) {
                        result << '[';
                        for (std::size_t j = 0; j < expression.match_value_binders[i].size(); ++j) {
                            if (j)
                                result << ", ";
                            result << expression.match_value_binders[i][j];
                        }
                        result << ']';
                    }
                    result << '(';
                    for (std::size_t j = 0; j < expression.match_proof_binders[i].size(); ++j) {
                        if (j)
                            result << ", ";
                        result << expression.match_proof_binders[i][j];
                    }
                    result << ") => " << print_proof(expression.match_bodies[i]);
                }
                return result.str() + " }";
            }
            case syntax::ProofExpr::Kind::hole: return "?";
            }
            return "<proof>";
        }

        std::string print_value_substituted(syntax::ValueExpr const &expression,
                                            std::map<std::string, std::string> const &substitutions) {
            if (expression.kind == syntax::ValueExpr::Kind::name) {
                if (auto found = substitutions.find(expression.name); found != substitutions.end())
                    return found->second;
                return expression.name;
            }
            if (expression.kind == syntax::ValueExpr::Kind::equal) {
                std::string left = print_value_substituted(expression.elements[0], substitutions);
                std::string right = print_value_substituted(expression.elements[1], substitutions);
                if (expression.elements[0].kind == syntax::ValueExpr::Kind::equal)
                    left = "(" + left + ")";
                if (expression.elements[1].kind == syntax::ValueExpr::Kind::equal)
                    right = "(" + right + ")";
                return left + " == " + right;
            }
            if (expression.kind == syntax::ValueExpr::Kind::call) {
                std::ostringstream result;
                result << expression.name << '(';
                for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                    if (i)
                        result << ", ";
                    result << print_value_substituted(expression.elements[i], substitutions);
                }
                result << ')';
                return result.str();
            }
            return print_value(expression);
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
            Elaborator(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                       std::string rainfall_run, ExecutionOptions options)
                : output_(output), options_(options) {
                if (rainfall_output)
                    rainfall_.emplace(context_, *rainfall_output, std::move(rainfall_run), snapshot);
            }

            ExecutionResult execute(syntax::Document const &document) {
                for (auto const &enumeration : document.enums)
                    declare_enum(enumeration);
                record_boundary();
                for (auto const &family : document.proof_inductives)
                    declare_proof_inductive(family);
                for (auto const &function : document.proof_functions)
                    declare_proof_function(function);
                for (auto const &function : document.functions)
                    declare_function(function);
                execute_run(document.run);
                if (rainfall_) {
                    rainfall_->validate_terms();
                    rainfall_->record(
                        "transition", "proof-core.run.close", {"run:" + document.run.name}, "fine.two-level-elaborator",
                        "All value terms checked; proof evidence remained virtual and every coeffect resolved",
                        {RainfallRecorder::string_field("status", "verified"),
                         RainfallRecorder::number_field("functions_verified", result_.functions_verified),
                         RainfallRecorder::number_field("proof_functions_verified", result_.proof_functions_verified),
                         RainfallRecorder::number_field("proofs_formed", result_.proofs_formed),
                         RainfallRecorder::number_field("proof_holes_filled", result_.proof_holes_filled),
                         RainfallRecorder::number_field("coeffects_resolved", result_.coeffects_resolved),
                         RainfallRecorder::number_field("runtime_proof_values", 0)});
                }
                output_ << "verified run: " << document.run.name << '\n' << "runtime-value-kinds: Int, Bool";
                for (auto const &[name, enumeration] : enums_)
                    output_ << ", " << name;
                output_ << '\n' << "runtime-proof-values: 0 (unrepresentable)\n";
                return result_;
            }

        private:
            z3::context context_;
            std::ostream &output_;
            ExecutionOptions options_;
            std::optional<RainfallRecorder> rainfall_;
            std::map<std::string, std::unique_ptr<RuntimeEnum>> enums_;
            std::map<std::string, std::pair<RuntimeEnum *, std::size_t>> constructors_;
            std::map<std::string, syntax::ProofInductiveDecl const *> proof_inductives_;
            std::map<std::string, syntax::ProofConstructorDecl const *> proof_constructors_;
            std::map<std::string, syntax::FunctionDecl const *> functions_;
            std::map<std::string, syntax::ProofFunctionDecl const *> proof_functions_;
            std::vector<std::string> proof_function_order_;
            syntax::ProofFunctionDecl const *active_inductive_function_ = nullptr;
            ExecutionResult result_;
            std::map<std::pair<std::size_t, std::size_t>, std::string> materializations_;
            std::size_t proof_model_index_ = 0;

            [[noreturn]] static void reject(syntax::SourceSpan span, std::string message) {
                throw SemanticError(span, std::move(message));
            }

            void request_materialization(syntax::ConcreteRange range, std::string text, syntax::SourceSpan span) {
                auto [found, inserted] = materializations_.emplace(std::pair{range.begin, range.end}, text);
                if (!inserted && found->second != text)
                    reject(span, "two materializations disagree at one source range");
            }

            z3::sort sort(ValueKind const &kind) {
                if (kind.tag == ValueKind::Tag::integer)
                    return context_.int_sort();
                if (kind.tag == ValueKind::Tag::boolean)
                    return context_.bool_sort();
                auto found = enums_.find(kind.name);
                if (found == enums_.end())
                    throw std::logic_error("unknown runtime enum kind `" + kind.name + "`");
                return found->second->sort;
            }

            void require_known_type(syntax::ValueType const &type) {
                if (type.kind == syntax::ValueType::Kind::enumeration && !enums_.contains(type.name))
                    reject(type.span, "unknown value type `" + type.name + "`");
            }

            void declare_enum(syntax::EnumDecl const &declaration) {
                if (declaration.name == "Int" || declaration.name == "Bool" || enums_.contains(declaration.name))
                    reject(declaration.span, "duplicate value type `" + declaration.name + "`");
                if (declaration.constructors.empty())
                    reject(declaration.span, "enum `" + declaration.name + "` has no constructors");

                ValueKind kind{ValueKind::Tag::enumeration, declaration.name};
                auto runtime = std::make_unique<RuntimeEnum>(context_, kind);
                z3::symbol sort_symbol = context_.str_symbol(("fine.enum." + declaration.name).c_str());
                z3::constructors z3_constructors(context_);
                std::set<std::string> local_constructors;
                std::vector<std::vector<ValueKind>> field_kinds;
                for (std::size_t constructor_index = 0; constructor_index < declaration.constructors.size();
                     ++constructor_index) {
                    auto const &constructor = declaration.constructors[constructor_index];
                    if (!local_constructors.insert(constructor.name).second || constructors_.contains(constructor.name))
                        reject(constructor.span, "duplicate enum constructor `" + constructor.name + "`");
                    std::vector<ValueKind> fields;
                    std::vector<z3::symbol> field_names;
                    std::vector<z3::sort> field_sorts;
                    for (std::size_t field_index = 0; field_index < constructor.fields.size(); ++field_index) {
                        auto const &field = constructor.fields[field_index];
                        if (field.kind == syntax::ValueType::Kind::enumeration && field.name != declaration.name)
                            require_known_type(field);
                        ValueKind field_kind = kind_of(field);
                        fields.push_back(field_kind);
                        field_names.push_back(
                            context_.str_symbol(("fine.enum." + declaration.name + "." + constructor.name + ".field" +
                                                 std::to_string(field_index))
                                                    .c_str()));
                        field_sorts.push_back(field_kind == kind ? context_.datatype_sort(sort_symbol)
                                                                 : sort(field_kind));
                    }
                    field_kinds.push_back(std::move(fields));
                    std::string z3_name = "fine.enum." + declaration.name + "." + constructor.name;
                    std::string z3_tester = "fine.enum." + declaration.name + ".is-" + constructor.name;
                    z3_constructors.add(context_.str_symbol(z3_name.c_str()), context_.str_symbol(z3_tester.c_str()),
                                        static_cast<unsigned>(field_names.size()),
                                        field_names.empty() ? nullptr : field_names.data(),
                                        field_sorts.empty() ? nullptr : field_sorts.data());
                }

                runtime->sort = context_.datatype(sort_symbol, z3_constructors);
                for (std::size_t i = 0; i < declaration.constructors.size(); ++i) {
                    auto const &source = declaration.constructors[i];
                    RuntimeConstructor constructor(context_, source.name, std::move(field_kinds[i]));
                    z3::func_decl_vector accessors(context_);
                    z3_constructors.query(static_cast<unsigned>(i), constructor.constructor, constructor.tester,
                                          accessors);
                    for (unsigned j = 0; j < accessors.size(); ++j)
                        constructor.accessors.push_back(accessors[j]);
                    runtime->constructors.push_back(std::move(constructor));
                }
                RuntimeEnum *stored = runtime.get();
                enums_.emplace(declaration.name, std::move(runtime));
                for (std::size_t i = 0; i < stored->constructors.size(); ++i)
                    constructors_.emplace(stored->constructors[i].name, std::pair{stored, i});
                output_ << "declared enum: " << declaration.name << " (" << stored->constructors.size()
                        << " constructors)\n";
            }

            void record_boundary() {
                if (!rainfall_)
                    return;
                std::vector<std::string> runtime_kinds{"Int", "Bool"};
                for (auto const &[name, enumeration] : enums_)
                    runtime_kinds.push_back(name);
                rainfall_->record(
                    "object", "proof.erasure.boundary", {}, "fine.two-level-core",
                    "The runtime value representation contains only value sorts; ProofEvidence is a "
                    "disjoint elaborator-only type",
                    {RainfallRecorder::raw_field("runtime_value_kinds", RainfallRecorder::string_array(runtime_kinds)),
                     RainfallRecorder::number_field("declared_runtime_enums", enums_.size()),
                     RainfallRecorder::number_field("runtime_proof_variants", 0),
                     RainfallRecorder::boolean_field("proof_evidence_elaboration_only", true)});
            }

            void ensure_fresh(std::string const &name, syntax::SourceSpan span, ValueEnvironment const &values,
                              ProofEnvironment const &proofs) {
                if (values.contains(name) || proofs.contains(name))
                    reject(span, "duplicate local name `" + name + "`");
            }

            ValueTerm elaborate_constructor(syntax::ValueExpr const &expression, RuntimeEnum &enumeration,
                                            RuntimeConstructor const &constructor, ValueEnvironment const &values,
                                            ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                            std::vector<z3::expr> const &absorbed) {
                if (!expression.using_proofs.empty())
                    reject(expression.span, "enum constructor `" + constructor.name + "` does not take proofs");
                if (expression.elements.size() != constructor.fields.size())
                    reject(expression.span, "enum constructor `" + constructor.name + "` expects " +
                                                std::to_string(constructor.fields.size()) + " fields");
                z3::expr_vector arguments(context_);
                for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                    ValueTerm field = elaborate_value(expression.elements[i], values, proofs, proof_order, absorbed);
                    if (field.kind != constructor.fields[i])
                        reject(expression.elements[i].span, "field " + std::to_string(i) + " of `" + constructor.name +
                                                                "` has the wrong value type");
                    arguments.push_back(field.expression);
                }
                return {enumeration.kind, constructor.constructor(arguments)};
            }

            ValueTerm elaborate_match(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                      ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                      std::vector<z3::expr> const &absorbed) {
                ValueTerm scrutinee = elaborate_value(expression.elements[0], values, proofs, proof_order, absorbed);
                if (scrutinee.kind.tag != ValueKind::Tag::enumeration)
                    reject(expression.elements[0].span, "match scrutinee is not an enum value");
                RuntimeEnum &enumeration = *enums_.at(scrutinee.kind.name);
                if (expression.match_constructors.empty())
                    reject(expression.span, "match has no arms");

                std::set<std::string> seen;
                std::vector<std::pair<z3::expr, ValueTerm>> branches;
                for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
                    std::string const &name = expression.match_constructors[i];
                    auto global = constructors_.find(name);
                    if (global == constructors_.end() || global->second.first != &enumeration)
                        reject(expression.match_arm_spans[i],
                               "constructor `" + name + "` does not belong to enum `" + enumeration.kind.name + "`");
                    if (!seen.insert(name).second)
                        reject(expression.match_arm_spans[i], "duplicate match arm for `" + name + "`");
                    RuntimeConstructor const &constructor = enumeration.constructors[global->second.second];
                    auto const &binders = expression.match_binders[i];
                    if (binders.size() != constructor.fields.size())
                        reject(expression.match_arm_spans[i], "match arm `" + name + "` expects " +
                                                                  std::to_string(constructor.fields.size()) +
                                                                  " binders");
                    ValueEnvironment branch_values = values;
                    std::set<std::string> arm_names;
                    for (std::size_t j = 0; j < binders.size(); ++j) {
                        if (!arm_names.insert(binders[j]).second)
                            reject(expression.match_arm_spans[i], "duplicate pattern binder `" + binders[j] + "`");
                        branch_values.insert_or_assign(
                            binders[j],
                            ValueTerm(constructor.fields[j], constructor.accessors[j](scrutinee.expression)));
                    }
                    ValueTerm body =
                        elaborate_value(expression.elements[i + 1], branch_values, proofs, proof_order, absorbed);
                    branches.emplace_back(constructor.tester(scrutinee.expression), std::move(body));
                }
                if (seen.size() != enumeration.constructors.size()) {
                    for (auto const &constructor : enumeration.constructors)
                        if (!seen.contains(constructor.name))
                            reject(expression.span, "non-exhaustive match: missing `" + constructor.name + "`");
                }
                ValueKind result_kind = branches.front().second.kind;
                for (auto const &branch : branches)
                    if (branch.second.kind != result_kind)
                        reject(expression.span, "match arms return different value types");
                z3::expr result = branches.back().second.expression;
                for (std::size_t i = branches.size() - 1; i-- > 0;)
                    result = z3::ite(branches[i].first, branches[i].second.expression, result);
                return {result_kind, std::move(result)};
            }

            ValueTerm elaborate_value(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                      ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                      std::vector<z3::expr> const &absorbed) {
                switch (expression.kind) {
                case syntax::ValueExpr::Kind::name: {
                    auto value = values.find(expression.name);
                    if (value != values.end())
                        return value->second;
                    if (proofs.contains(expression.name))
                        reject(expression.span, "proof `" + expression.name + "` cannot inhabit a runtime value");
                    if (proof_constructors_.contains(expression.name))
                        reject(expression.span,
                               "proof constructor `" + expression.name + "` cannot inhabit a runtime value");
                    if (auto found = constructors_.find(expression.name); found != constructors_.end()) {
                        RuntimeConstructor const &constructor = found->second.first->constructors[found->second.second];
                        if (!constructor.fields.empty())
                            reject(expression.span, "enum constructor `" + expression.name + "` expects " +
                                                        std::to_string(constructor.fields.size()) + " fields");
                        syntax::ValueExpr nullary = expression;
                        nullary.kind = syntax::ValueExpr::Kind::call;
                        return elaborate_constructor(nullary, *found->second.first, constructor, values, proofs,
                                                     proof_order, absorbed);
                    }
                    reject(expression.span, "unknown value `" + expression.name + "`");
                }
                case syntax::ValueExpr::Kind::integer:
                    return {integer_kind(), context_.int_val(expression.integer_text.c_str())};
                case syntax::ValueExpr::Kind::boolean:
                    return {boolean_kind(), context_.bool_val(expression.boolean_value)};
                case syntax::ValueExpr::Kind::equal: {
                    ValueTerm left = elaborate_value(expression.elements[0], values, proofs, proof_order, absorbed);
                    ValueTerm right = elaborate_value(expression.elements[1], values, proofs, proof_order, absorbed);
                    if (left.kind != right.kind)
                        reject(expression.span, "equality operands have different value types");
                    return {boolean_kind(), left.expression == right.expression};
                }
                case syntax::ValueExpr::Kind::call: {
                    if (auto found = constructors_.find(expression.name); found != constructors_.end())
                        return elaborate_constructor(expression, *found->second.first,
                                                     found->second.first->constructors[found->second.second], values,
                                                     proofs, proof_order, absorbed);
                    return elaborate_call(expression, values, proofs, proof_order, absorbed);
                }
                case syntax::ValueExpr::Kind::match:
                    return elaborate_match(expression, values, proofs, proof_order, absorbed);
                }
                reject(expression.span, "unsupported value expression");
            }

            IdentityType elaborate_identity(syntax::ProofType const &type, ValueEnvironment const &values,
                                            ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                            std::vector<z3::expr> const &absorbed) {
                if (type.kind != syntax::ProofType::Kind::identity)
                    reject(type.span, "expected identity proof type, found `" + print_proof_type(type) + "`");
                require_known_type(type.carrier);
                ValueKind carrier = kind_of(type.carrier);
                ValueTerm left = elaborate_value(type.left, values, proofs, proof_order, absorbed);
                ValueTerm right = elaborate_value(type.right, values, proofs, proof_order, absorbed);
                if (left.kind != carrier || right.kind != carrier)
                    reject(type.span,
                           "identity endpoints do not have carrier type `" + std::string(kind_name(carrier)) + "`");
                return {carrier, std::move(left.expression), std::move(right.expression)};
            }

            InductiveType elaborate_inductive_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                                   ProofEnvironment const &proofs,
                                                   std::vector<std::string> const &proof_order,
                                                   std::vector<z3::expr> const &absorbed) {
                auto found = proof_inductives_.find(type.name);
                if (found == proof_inductives_.end())
                    reject(type.span, "unknown proof inductive `" + type.name + "`");
                auto const &family = *found->second;
                if (type.arguments.size() != family.indices.size())
                    reject(type.span, "proof inductive `" + type.name + "` expects " +
                                          std::to_string(family.indices.size()) + " indices");
                InductiveType result{type.name, {}};
                for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                    ValueTerm index = elaborate_value(type.arguments[i], values, proofs, proof_order, absorbed);
                    require_known_type(family.indices[i].type);
                    if (index.kind != kind_of(family.indices[i].type))
                        reject(type.arguments[i].span, "index `" + family.indices[i].name + "` of `" + type.name +
                                                           "` has the wrong value type");
                    result.indices.push_back(std::move(index));
                }
                return result;
            }

            SemanticProofType elaborate_proof_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                                   ProofEnvironment const &proofs,
                                                   std::vector<std::string> const &proof_order,
                                                   std::vector<z3::expr> const &absorbed) {
                if (type.kind == syntax::ProofType::Kind::identity)
                    return elaborate_identity(type, values, proofs, proof_order, absorbed);
                return elaborate_inductive_type(type, values, proofs, proof_order, absorbed);
            }

            bool contains_parameter(syntax::ValueExpr const &expression,
                                    std::map<std::string, ValueKind> const &parameters,
                                    ValueEnvironment const &bindings) {
                if (expression.kind == syntax::ValueExpr::Kind::name && parameters.contains(expression.name) &&
                    !bindings.contains(expression.name))
                    return true;
                return std::any_of(expression.elements.begin(), expression.elements.end(),
                                   [&](syntax::ValueExpr const &element) {
                                       return contains_parameter(element, parameters, bindings);
                                   });
            }

            bool match_constructor_index(syntax::ValueExpr const &pattern, z3::expr const &target,
                                         std::map<std::string, ValueKind> const &parameters,
                                         ValueEnvironment &bindings) {
                if (pattern.kind == syntax::ValueExpr::Kind::name && parameters.contains(pattern.name)) {
                    ValueKind kind = parameters.at(pattern.name);
                    if (!Z3_is_eq_sort(context_, target.get_sort(), sort(kind)))
                        return false;
                    auto found = bindings.find(pattern.name);
                    if (found != bindings.end())
                        return same_ast(context_, found->second.expression, target);
                    bindings.emplace(pattern.name, ValueTerm(kind, target));
                    return true;
                }

                auto match_runtime_constructor = [&](std::string const &name,
                                                     std::vector<syntax::ValueExpr> const &arguments) {
                    auto found = constructors_.find(name);
                    if (found == constructors_.end() || !target.is_app())
                        return false;
                    RuntimeConstructor const &constructor =
                        found->second.first->constructors[found->second.second];
                    if (arguments.size() != constructor.fields.size() || target.num_args() != arguments.size() ||
                        !Z3_is_eq_func_decl(context_, target.decl(), constructor.constructor))
                        return false;
                    for (std::size_t i = 0; i < arguments.size(); ++i)
                        if (!match_constructor_index(arguments[i], target.arg(static_cast<unsigned>(i)), parameters,
                                                     bindings))
                            return false;
                    return true;
                };

                if (pattern.kind == syntax::ValueExpr::Kind::name && constructors_.contains(pattern.name))
                    return match_runtime_constructor(pattern.name, {});
                if (pattern.kind == syntax::ValueExpr::Kind::call && constructors_.contains(pattern.name))
                    return match_runtime_constructor(pattern.name, pattern.elements);
                if (contains_parameter(pattern, parameters, bindings))
                    return false;
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                ValueTerm instantiated = elaborate_value(pattern, bindings, no_proofs, no_proof_order, no_absorbed);
                return same_ast(context_, instantiated.expression, target);
            }

            bool is_refinable_index(syntax::ValueExpr const &source, ValueTerm const &term) {
                if (source.kind != syntax::ValueExpr::Kind::name || !term.expression.is_app() ||
                    term.expression.num_args() != 0 || term.expression.decl().decl_kind() != Z3_OP_UNINTERPRETED)
                    return false;
                return term.expression.decl().name().str().starts_with("fine.proof-function.");
            }

            bool bind_result_index(syntax::ValueExpr const &pattern, z3::expr const &target,
                                   std::string const &target_source, std::map<std::string, ValueKind> const &parameters,
                                   ValueEnvironment &bindings, std::map<std::string, std::string> &source_bindings) {
                if (pattern.kind != syntax::ValueExpr::Kind::name || !parameters.contains(pattern.name))
                    return true;
                ValueKind kind = parameters.at(pattern.name);
                if (!Z3_is_eq_sort(context_, target.get_sort(), sort(kind)))
                    return false;
                if (auto found = bindings.find(pattern.name); found != bindings.end())
                    return same_ast(context_, found->second.expression, target);
                bindings.emplace(pattern.name, ValueTerm(kind, target));
                source_bindings.emplace(pattern.name, target_source);
                return true;
            }

            bool value_pattern_ready(syntax::ValueExpr const &pattern,
                                     std::map<std::string, ValueKind> const &parameters,
                                     ValueEnvironment const &bindings) {
                if (pattern.kind == syntax::ValueExpr::Kind::name && parameters.contains(pattern.name))
                    return bindings.contains(pattern.name);
                return std::all_of(pattern.elements.begin(), pattern.elements.end(),
                                   [&](syntax::ValueExpr const &element) {
                                       return value_pattern_ready(element, parameters, bindings);
                                   });
            }

            bool match_index_pattern(syntax::ValueExpr const &pattern, z3::expr const &target,
                                     std::string const &target_source,
                                     std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings,
                                     std::map<std::string, std::string> &source_bindings, bool &added) {
                std::size_t before = bindings.size();
                if (!bind_result_index(pattern, target, target_source, parameters, bindings, source_bindings))
                    return false;
                added = added || bindings.size() != before;
                if (!value_pattern_ready(pattern, parameters, bindings))
                    return true;
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                ValueTerm instantiated = elaborate_value(pattern, bindings, no_proofs, no_proof_order, no_absorbed);
                return same_ast(context_, instantiated.expression, target);
            }

            bool match_identity_pattern(syntax::ProofType const &pattern, ProofEvidence const &target,
                                        std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings,
                                        std::map<std::string, std::string> &source_bindings, bool &added) {
                auto identity = std::get_if<IdentityType>(&target.type);
                if (!identity)
                    return false;
                return kind_of(pattern.carrier) == identity->carrier &&
                       match_index_pattern(pattern.left, identity->left, target.left_source, parameters, bindings,
                                           source_bindings, added) &&
                       match_index_pattern(pattern.right, identity->right, target.right_source, parameters, bindings,
                                           source_bindings, added);
            }

            bool complete_index_instantiation(syntax::ProofFunctionDecl const &function, IdentityType const &expected,
                                              IndexInstantiation const &instantiation) {
                if (instantiation.values.size() != function.parameters.size())
                    return false;
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                IdentityType result = elaborate_identity(function.result_type, instantiation.values, no_proofs,
                                                         no_proof_order, no_absorbed);
                return same_type(context_, result, expected);
            }

            std::string index_instantiation_key(syntax::ProofFunctionDecl const &function,
                                                IndexInstantiation const &instantiation) {
                std::ostringstream key;
                for (auto const &parameter : function.parameters) {
                    key << parameter.name << '=';
                    if (auto found = instantiation.values.find(parameter.name); found != instantiation.values.end())
                        key << Z3_get_ast_id(context_, found->second.expression);
                    key << ';';
                }
                return key.str();
            }

            std::vector<IndexInstantiation>
            infer_value_arguments(syntax::ProofFunctionDecl const &function, IdentityType const &expected,
                                  std::string const &left_source, std::string const &right_source,
                                  ProofEnvironment const &proofs, std::vector<std::string> const &proof_order) {
                if (kind_of(function.result_type.carrier) != expected.carrier)
                    return {};
                std::map<std::string, ValueKind> parameters;
                for (auto const &parameter : function.parameters)
                    parameters.emplace(parameter.name, kind_of(parameter.type));
                IndexInstantiation initial;
                if (!bind_result_index(function.result_type.left, expected.left, left_source, parameters,
                                       initial.values, initial.sources) ||
                    !bind_result_index(function.result_type.right, expected.right, right_source, parameters,
                                       initial.values, initial.sources))
                    return {};

                std::vector<IndexInstantiation> completed;
                std::set<std::string> visited;
                auto search = [&](auto &&self, IndexInstantiation instantiation) -> void {
                    std::string key = index_instantiation_key(function, instantiation);
                    if (!visited.insert(key).second)
                        return;
                    if (complete_index_instantiation(function, expected, instantiation)) {
                        completed.push_back(std::move(instantiation));
                        return;
                    }
                    for (auto const &parameter : function.proof_parameters) {
                        for (auto const &proof_name : proof_order) {
                            auto found = proofs.find(proof_name);
                            if (found == proofs.end())
                                continue;
                            IndexInstantiation extended = instantiation;
                            bool added = false;
                            if (match_identity_pattern(parameter.type, found->second, parameters, extended.values,
                                                       extended.sources, added) &&
                                added)
                                self(self, std::move(extended));
                        }
                    }
                };
                search(search, std::move(initial));
                return completed;
            }

            std::optional<IndexInstantiation>
            infer_inductive_value_arguments(syntax::ProofFunctionDecl const &function,
                                             syntax::ProofType const &expected_syntax,
                                             InductiveType const &expected) {
                if (function.result_type.kind != syntax::ProofType::Kind::inductive ||
                    function.result_type.name != expected.family ||
                    function.result_type.arguments.size() != expected.indices.size() ||
                    expected_syntax.arguments.size() != expected.indices.size())
                    return std::nullopt;
                std::map<std::string, ValueKind> parameters;
                for (auto const &parameter : function.parameters)
                    parameters.emplace(parameter.name, kind_of(parameter.type));
                IndexInstantiation result;
                bool added = false;
                for (std::size_t i = 0; i < expected.indices.size(); ++i)
                    if (!match_index_pattern(function.result_type.arguments[i], expected.indices[i].expression,
                                             print_value(expected_syntax.arguments[i]), parameters, result.values,
                                             result.sources, added))
                        return std::nullopt;
                if (result.values.size() != function.parameters.size())
                    return std::nullopt;
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                SemanticProofType instantiated = elaborate_proof_type(
                    function.result_type, result.values, no_proofs, no_proof_order, no_absorbed);
                if (!same_type(context_, instantiated, SemanticProofType(expected)))
                    return std::nullopt;
                return result;
            }

            proof_model::Type proof_model_type(IdentityType const &type) {
                return {sort(type.carrier).id(), Z3_get_ast_id(context_, type.left),
                        Z3_get_ast_id(context_, type.right)};
            }

            std::string proof_model_type_key(proof_model::Type const &type) {
                return std::to_string(type.carrier) + ":" + std::to_string(type.left) + ":" +
                       std::to_string(type.right);
            }

            void collect_proof_model_production(ProofCandidate const &candidate, proof_model::Grammar &grammar,
                                                std::set<std::string> &seen) {
                if (!candidate.type)
                    throw std::logic_error("typed proof candidate lost its result type");
                proof_model::Production production;
                production.result = proof_model_type(*candidate.type);
                std::string key;
                if (candidate.local_proof) {
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

            proof_model::Grammar make_proof_model_grammar(std::vector<ProofCandidate> const &candidates,
                                                          IdentityType const &expected) {
                proof_model::Grammar grammar;
                grammar.id = "hole-" + std::to_string(proof_model_index_++);
                grammar.expected = proof_model_type(expected);
                grammar.max_cost = max_proof_search_cost;
                std::set<std::string> seen;
                for (auto const &candidate : candidates)
                    collect_proof_model_production(candidate, grammar, seen);
                return grammar;
            }

            ProofEvidence elaborate_proof_application(syntax::ProofExpr const &expression,
                                                      syntax::ProofType const &expected_syntax,
                                                      SemanticProofType expected,
                                                      ValueEnvironment const &values, ProofEnvironment const &proofs,
                                                      std::vector<std::string> const &proof_order,
                                                      std::vector<z3::expr> const &absorbed, std::string name,
                                                      std::string const &run) {
                auto found = proof_functions_.find(expression.name);
                if (found == proof_functions_.end()) {
                    if (functions_.contains(expression.name))
                        reject(expression.span, "value function `" + expression.name + "` cannot inhabit a proof");
                    reject(expression.span, "unknown proof function `" + expression.name + "`");
                }
                syntax::ProofFunctionDecl const &function = *found->second;
                if (expression.value_arguments.size() != function.parameters.size())
                    reject(expression.span, "proof function `" + expression.name + "` expects " +
                                                std::to_string(function.parameters.size()) + " index arguments");
                if (expression.proof_arguments.size() != function.proof_parameters.size())
                    reject(expression.span, "proof function `" + expression.name + "` expects " +
                                                std::to_string(function.proof_parameters.size()) + " proof arguments");

                bool induction_hypothesis_use = active_inductive_function_ == &function;
                std::string recursive_evidence;
                std::string recursive_parent;
                if (induction_hypothesis_use) {
                    auto parameter = std::find_if(
                        function.proof_parameters.begin(), function.proof_parameters.end(),
                        [&](syntax::CoeffectParameter const &candidate) {
                            return candidate.name == *function.induction_parameter;
                        });
                    if (parameter == function.proof_parameters.end())
                        throw std::logic_error("active induction parameter disappeared");
                    std::size_t position = static_cast<std::size_t>(parameter - function.proof_parameters.begin());
                    auto const &argument = expression.proof_arguments[position];
                    if (argument.kind != syntax::ProofExpr::Kind::name)
                        reject(argument.span, "recursive proof call `" + function.name +
                                                  "` must use a named recursive constructor field for `" +
                                                  *function.induction_parameter + "`");
                    auto evidence = proofs.find(argument.name);
                    if (evidence == proofs.end() || !evidence->second.structural_root ||
                        *evidence->second.structural_root != *function.induction_parameter)
                        reject(argument.span, "recursive proof call `" + function.name + "` does not descend through "
                                              "a proof field of induction parameter `" +
                                                  *function.induction_parameter + "`");
                    recursive_evidence = argument.name;
                    recursive_parent = evidence->second.structural_parent.value_or("");
                }

                ValueEnvironment indices;
                for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                    ValueTerm argument =
                        elaborate_value(expression.value_arguments[i], values, proofs, proof_order, absorbed);
                    ValueKind expected_kind = kind_of(function.parameters[i].type);
                    if (argument.kind != expected_kind)
                        reject(expression.value_arguments[i].span,
                               "index argument `" + function.parameters[i].name + "` has the wrong value type");
                    indices.emplace(function.parameters[i].name, std::move(argument));
                }
                ProofEnvironment no_proofs;
                std::vector<std::string> no_proof_order;
                std::vector<z3::expr> no_absorbed;
                SemanticProofType result_type =
                    elaborate_proof_type(function.result_type, indices, no_proofs, no_proof_order, no_absorbed);
                if (!same_type(context_, result_type, expected))
                    reject(expression.span,
                           "proof application `" + print_proof(expression) + "` has the wrong result type");

                std::vector<std::string> index_sources;
                for (auto const &argument : expression.value_arguments)
                    index_sources.push_back(print_value(argument));
                std::vector<std::string> argument_sources;
                for (std::size_t i = 0; i < function.proof_parameters.size(); ++i) {
                    if (expression.proof_arguments[i].kind == syntax::ProofExpr::Kind::hole)
                        reject(expression.proof_arguments[i].span, "nested proof holes are not admitted in this slice");
                    SemanticProofType parameter_type = elaborate_proof_type(
                        function.proof_parameters[i].type, indices, no_proofs, no_proof_order, no_absorbed);
                    ProofEvidence argument = elaborate_any_proof(
                        expression.proof_arguments[i], function.proof_parameters[i].type, std::move(parameter_type),
                        values, proofs, proof_order, absorbed, name + "." + function.proof_parameters[i].name, run,
                        {}, {});
                    argument_sources.push_back(print_proof(expression.proof_arguments[i]));
                }

                if (rainfall_ && induction_hypothesis_use)
                    rainfall_->record(
                        "derive", "proof.induction.hypothesis.use", {"proof-function:" + run},
                        "fine.proof-elaborator",
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
                         RainfallRecorder::raw_field("proof_arguments",
                                                     RainfallRecorder::string_array(argument_sources)),
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
                return {std::move(name), std::move(expected),
                        induction_hypothesis_use ? "induction-hypothesis:" + function.name
                                                 : "apply:" + function.name,
                        expression.span,
                        std::move(left_source), std::move(right_source), std::move(index_syntax)};
            }

            std::vector<ProofCandidate>
            enumerate_proof_candidates(syntax::ProofType const &expected_syntax, IdentityType const &expected,
                                       std::string const &left_source, std::string const &right_source,
                                       ValueEnvironment const &values, ProofEnvironment const &proofs,
                                       std::vector<std::string> const &proof_order,
                                       std::vector<z3::expr> const &absorbed, std::size_t budget) {
                if (budget == 0)
                    return {};
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
                            IdentityType argument_type = elaborate_identity(parameter.type, instantiation.values,
                                                                            no_proofs, no_proof_order, no_absorbed);
                            std::string argument_left =
                                print_value_substituted(parameter.type.left, instantiation.sources);
                            std::string argument_right =
                                print_value_substituted(parameter.type.right, instantiation.sources);
                            auto frontier =
                                enumerate_proof_candidates(parameter.type, argument_type, argument_left, argument_right,
                                                           values, proofs, proof_order, absorbed, budget - 1);
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
                            source << function.name;
                            std::vector<std::string> index_arguments;
                            if (!function.parameters.empty()) {
                                source << '[';
                                for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                                    if (i)
                                        source << ", ";
                                    std::string const &argument = instantiation.sources.at(function.parameters[i].name);
                                    source << argument;
                                    index_arguments.push_back(argument);
                                }
                                source << ']';
                            }
                            source << '(';
                            for (std::size_t i = 0; i < argument_sources.size(); ++i) {
                                if (i)
                                    source << ", ";
                                source << argument_sources[i];
                            }
                            source << ')';
                            std::vector<ProofCandidate> children = arguments;
                            candidates.push_back({source.str(), "proof-application", std::nullopt, function.name,
                                                  std::move(index_arguments), std::move(argument_sources), cost,
                                                  expected, std::move(children)});
                        }
                    }
                }
                return candidates;
            }

            ProofEvidence elaborate_proof(syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax,
                                          IdentityType expected, ValueEnvironment const &values,
                                          ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
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
                        reject(expression.span,
                               "`refl` requires both identity endpoints to elaborate to its exact value");
                    return {std::move(name),
                            std::move(expected),
                            "refl",
                            expression.span,
                            print_value(expected_syntax.left),
                            print_value(expected_syntax.right)};
                }
                if (expression.kind == syntax::ProofExpr::Kind::application)
                    return elaborate_proof_application(
                        expression, expected_syntax, SemanticProofType(std::move(expected)), values, proofs,
                        proof_order, absorbed, std::move(name), run);

                if (options_.require_materialized_proofs)
                    reject(expression.span, "proof hole remains after materialization");

                std::vector<ProofCandidate> candidates = enumerate_proof_candidates(
                    expected_syntax, expected, print_value(expected_syntax.left), print_value(expected_syntax.right),
                    values, proofs, proof_order, absorbed, max_proof_search_cost);

                std::string proposition;
                std::string hole = "proof-hole:" + proof_source;
                std::vector<std::string> candidate_events;
                if (rainfall_) {
                    proposition = rainfall_->term(expected.left == expected.right, "proof-hole-proposition");
                    rainfall_->record(
                        "object", "proof.search.open", {"run:" + run, hole}, "fine.typed-proof-search",
                        "A source proof hole opens with a finite grammar determined by its expected identity type",
                        {RainfallRecorder::string_field("id", hole),
                         RainfallRecorder::string_field("source", proof_source),
                         RainfallRecorder::string_field("type_source", type_source),
                         RainfallRecorder::string_field("binding", name),
                         RainfallRecorder::string_field("expected_type", print_identity(expected_syntax)),
                         RainfallRecorder::string_field("proposition", proposition),
                         RainfallRecorder::raw_field("grammar", "[\"exact-local\",\"refl\",\"proof-application\"]"),
                         RainfallRecorder::number_field("max_cost", max_proof_search_cost),
                         RainfallRecorder::boolean_field("ill_typed_candidates_enumerated", false)});
                    for (auto const &candidate : candidates) {
                        std::vector<RainfallField> data = {
                            RainfallRecorder::string_field("hole", hole),
                            RainfallRecorder::string_field("body", candidate.source),
                            RainfallRecorder::string_field("production", candidate.production),
                            RainfallRecorder::string_field("expected_type", print_identity(expected_syntax)),
                            RainfallRecorder::boolean_field("exact_type", true),
                            RainfallRecorder::boolean_field("runtime_value_created", false),
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
                    auto found = std::find_if(candidates.begin(), candidates.end(), [&](ProofCandidate const &item) {
                        return item.source == model_selection->source && item.cost == model_selection->cost;
                    });
                    if (found == candidates.end())
                        reject(expression.span,
                               "Z3 proof model lifted a tree outside the deterministic Fine frontier: `" +
                                   model_selection->source + "`");
                    selected_index = static_cast<std::size_t>(found - candidates.begin());
                }

                ProofCandidate const &selected = candidates[selected_index];
                request_materialization(syntax::ConcreteRange::from_span(expression.span), selected.source,
                                        expression.span);
                ++result_.proof_holes_filled;
                if (rainfall_) {
                    if (model_grammar && model_selection) {
                        std::vector<std::string> productions;
                        for (auto const &production : model_grammar->productions) {
                            if (production.kind == proof_model::ProductionKind::application) {
                                std::ostringstream description;
                                description << "apply:" << production.function;
                                if (!production.index_arguments.empty()) {
                                    description << '[';
                                    for (std::size_t i = 0; i < production.index_arguments.size(); ++i) {
                                        if (i)
                                            description << ", ";
                                        description << production.index_arguments[i];
                                    }
                                    description << ']';
                                }
                                description << '/' << production.arguments.size();
                                productions.push_back(description.str());
                            }
                            else {
                                productions.push_back(
                                    (production.kind == proof_model::ProductionKind::local ? "local:" : "refl:") +
                                    production.source);
                            }
                        }
                        std::string grammar_event = rainfall_->record(
                            "object", "proof.model.grammar", {"run:" + run, hole}, "fine.proof-model-selector",
                            "The exact deterministic Fine frontier is compacted into ground recursive datatype "
                            "productions without changing its bound or source owners",
                            {RainfallRecorder::string_field("hole", hole),
                             RainfallRecorder::string_field("grammar", model_grammar->id),
                             RainfallRecorder::number_field("max_cost", model_grammar->max_cost),
                             RainfallRecorder::raw_field("productions", RainfallRecorder::string_array(productions)),
                             RainfallRecorder::raw_field("reference_candidates",
                                                         RainfallRecorder::string_array(candidate_events))});
                        std::string solve_event = rainfall_->record(
                            "derive", "proof.model.solve", {"run:" + run, hole, grammar_event},
                            "fine.proof-model-selector",
                            "Z3 assigns the bounded recursive proof datatype a ground constructor tree",
                            {RainfallRecorder::string_field("hole", hole),
                             RainfallRecorder::string_field("grammar_event", grammar_event),
                             RainfallRecorder::string_field("grammar", model_grammar->id),
                             RainfallRecorder::string_field("status", "sat"),
                             RainfallRecorder::string_field("model_value", model_selection->model_value),
                             RainfallRecorder::number_field("cost", model_selection->cost)});
                        rainfall_->record(
                            "transform", "proof.model.lift", {"run:" + run, hole, solve_event},
                            "fine.proof-model-selector",
                            "The model constructor tree lifts to Fine source and matches one exact reference "
                            "candidate before materialization",
                            {RainfallRecorder::string_field("hole", hole),
                             RainfallRecorder::string_field("solve_event", solve_event),
                             RainfallRecorder::string_field("body", model_selection->source),
                             RainfallRecorder::string_field("candidate", candidate_events[selected_index]),
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
                         RainfallRecorder::string_field("production", selected.production)});
                    std::vector<std::string> residual;
                    for (std::size_t i = 0; i < candidate_events.size(); ++i)
                        if (i != selected_index)
                            residual.push_back(candidate_events[i]);
                    rainfall_->record(
                        "transition", "proof.search.close", {"run:" + run, hole}, "fine.typed-proof-search",
                        "The typed hole has a checked source witness and its unchosen finite frontier remains explicit",
                        {RainfallRecorder::string_field("hole", hole),
                         RainfallRecorder::string_field("selection", selection),
                         RainfallRecorder::string_field("selected_candidate", selected_event),
                         RainfallRecorder::raw_field("residual_candidates", RainfallRecorder::string_array(residual)),
                         RainfallRecorder::string_field("status", "selected"),
                         RainfallRecorder::boolean_field("materialization_requested", true)});
                }
                output_ << "filled proof hole: " << name << " <- " << selected.source << " ("
                        << (options_.proof_selector == ProofSelector::z3_model ? "Z3 datatype model" : "typed search")
                        << ")\n";
                std::string formation;
                if (selected.local_proof)
                    formation = "exact-local:" + *selected.local_proof;
                else if (selected.proof_function)
                    formation = "proof-application:" + *selected.proof_function;
                else
                    formation = "refl";
                formation =
                    (options_.proof_selector == ProofSelector::z3_model ? "search:z3-model:" : "search:") + formation;
                return {std::move(name),
                        std::move(expected),
                        std::move(formation),
                        expression.span,
                        print_value(expected_syntax.left),
                        print_value(expected_syntax.right)};
            }

            std::vector<ProofCandidate>
            enumerate_inductive_proof_candidates(syntax::ProofType const &expected_syntax,
                                                 InductiveType const &expected,
                                                 ProofEnvironment const &proofs,
                                                 std::vector<std::string> const &proof_order) {
                std::vector<ProofCandidate> candidates;
                for (auto const &candidate_name : proof_order) {
                    auto found = proofs.find(candidate_name);
                    if (found == proofs.end())
                        continue;
                    auto inductive = std::get_if<InductiveType>(&found->second.type);
                    if (inductive && same_type(context_, *inductive, expected))
                        candidates.push_back({candidate_name, "exact-local", candidate_name, std::nullopt,
                                              {}, {}, 1, std::nullopt, {}});
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
                    SemanticProofType parameter_type = elaborate_proof_type(
                        parameter.type, instantiation->values, no_proofs, no_proof_order, no_absorbed);
                    std::vector<std::string> frontier;
                    for (auto const &proof_name : proof_order) {
                        auto found = proofs.find(proof_name);
                        if (found == proofs.end() || !same_type(context_, found->second.type, parameter_type))
                            continue;
                        if (parameter.name == *function.induction_parameter &&
                            (!found->second.structural_root ||
                             *found->second.structural_root != *function.induction_parameter))
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
                    source << function.name;
                    std::vector<std::string> index_arguments;
                    if (!function.parameters.empty()) {
                        source << '[';
                        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                            if (i)
                                source << ", ";
                            std::string const &argument = instantiation->sources.at(function.parameters[i].name);
                            source << argument;
                            index_arguments.push_back(argument);
                        }
                        source << ']';
                    }
                    source << '(';
                    for (std::size_t i = 0; i < arguments.size(); ++i) {
                        if (i)
                            source << ", ";
                        source << arguments[i];
                    }
                    source << ')';
                    candidates.push_back({source.str(), "induction-hypothesis", std::nullopt, function.name,
                                          std::move(index_arguments), arguments, cost, std::nullopt, {}});
                }
                return candidates;
            }

            ProofEvidence elaborate_inductive_hole(syntax::ProofExpr const &expression,
                                                    syntax::ProofType const &expected_syntax,
                                                    InductiveType expected,
                                                    ProofEnvironment const &proofs,
                                                    std::vector<std::string> const &proof_order,
                                                    std::string name, std::string const &run) {
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
                    proof_source = rainfall_->source_node(expression.node_id, expression.span,
                                                          "proof.expression.hole");
                    type_source = rainfall_->source_node(expected_syntax.node_id, expected_syntax.span,
                                                         "proof-type.inductive");
                    hole = "proof-hole:" + proof_source;
                    rainfall_->record(
                        "object", "proof.search.open", {"run:" + run, hole}, "fine.typed-proof-search",
                        "An indexed proof hole opens with exact local evidence and structurally admitted induction "
                        "hypothesis applications only",
                        {RainfallRecorder::string_field("id", hole),
                         RainfallRecorder::string_field("source", proof_source),
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
                            RainfallRecorder::number_field("cost", candidate.cost),
                        };
                        if (candidate.local_proof)
                            data.push_back(RainfallRecorder::string_field("proof", *candidate.local_proof));
                        if (candidate.proof_function) {
                            auto const &function = *active_inductive_function_;
                            auto parameter = std::find_if(
                                function.proof_parameters.begin(), function.proof_parameters.end(),
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
                            data.push_back(RainfallRecorder::string_field(
                                "induction_parameter", *function.induction_parameter));
                            data.push_back(RainfallRecorder::string_field(
                                "parent_evidence", evidence.structural_parent.value_or("")));
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
                request_materialization(syntax::ConcreteRange::from_span(expression.span), selected.source,
                                        expression.span);
                ++result_.proof_holes_filled;
                if (rainfall_) {
                    std::string selection = rainfall_->record(
                        "transition", "proof.search.select", {"run:" + run, hole}, "fine.typed-proof-search",
                        "The first deterministic exact indexed proof candidate is selected for materialization",
                        {RainfallRecorder::string_field("hole", hole),
                         RainfallRecorder::string_field("candidate", candidate_events.front()),
                         RainfallRecorder::string_field("body", selected.source),
                         RainfallRecorder::string_field("production", selected.production)});
                    std::vector<std::string> residual(candidate_events.begin() + 1, candidate_events.end());
                    rainfall_->record(
                        "transition", "proof.search.close", {"run:" + run, hole}, "fine.typed-proof-search",
                        "The indexed hole has a checked source witness and a complete residual frontier",
                        {RainfallRecorder::string_field("hole", hole),
                         RainfallRecorder::string_field("selection", selection),
                         RainfallRecorder::string_field("selected_candidate", candidate_events.front()),
                         RainfallRecorder::raw_field("residual_candidates",
                                                     RainfallRecorder::string_array(residual)),
                         RainfallRecorder::string_field("status", "selected"),
                         RainfallRecorder::boolean_field("materialization_requested", true)});
                }
                output_ << "filled proof hole: " << name << " <- " << selected.source
                        << " (typed search)\n";
                std::string formation = selected.local_proof
                                            ? "search:exact-local:" + *selected.local_proof
                                            : "search:induction-hypothesis:" + *selected.proof_function;
                return {std::move(name), std::move(expected), std::move(formation), expression.span, "", "",
                        expected_syntax.arguments};
            }

            ProofEvidence elaborate_inductive_proof(syntax::ProofExpr const &expression,
                                                    syntax::ProofType const &expected_syntax, InductiveType expected,
                                                    ValueEnvironment const &values, ProofEnvironment const &proofs,
                                                    std::vector<std::string> const &proof_order,
                                                    std::vector<z3::expr> const &absorbed, std::string name,
                                                    std::string const &run) {
                if (expression.kind == syntax::ProofExpr::Kind::name) {
                    auto found = proofs.find(expression.name);
                    if (found == proofs.end())
                        reject(expression.span, "unknown proof `" + expression.name + "`");
                    auto inductive = std::get_if<InductiveType>(&found->second.type);
                    if (!inductive || !same_type(context_, *inductive, expected))
                        reject(expression.span, "proof `" + expression.name + "` has the wrong inductive type");
                    return {std::move(name), std::move(expected), "alias:" + expression.name, expression.span, "", "",
                            found->second.index_syntax};
                }
                if (expression.kind == syntax::ProofExpr::Kind::reflexivity)
                    reject(expression.span, "`refl` constructs identity evidence, not `" + expected.family + "`");
                if (expression.kind == syntax::ProofExpr::Kind::hole)
                    return elaborate_inductive_hole(expression, expected_syntax, std::move(expected), proofs,
                                                    proof_order, std::move(name), run);

                auto found = proof_constructors_.find(expression.name);
                if (found == proof_constructors_.end()) {
                    if (proof_functions_.contains(expression.name))
                        return elaborate_proof_application(
                            expression, expected_syntax, SemanticProofType(std::move(expected)), values, proofs,
                            proof_order, absorbed, std::move(name), run);
                    reject(expression.span, "unknown proof constructor `" + expression.name + "`");
                }
                auto const &constructor = *found->second;
                if (expression.value_arguments.size() != constructor.parameters.size())
                    reject(expression.span, "proof constructor `" + expression.name + "` expects " +
                                                std::to_string(constructor.parameters.size()) + " index arguments");
                if (expression.proof_arguments.size() != constructor.proof_parameters.size())
                    reject(expression.span, "proof constructor `" + expression.name + "` expects " +
                                                std::to_string(constructor.proof_parameters.size()) +
                                                " proof arguments");

                ValueEnvironment indices;
                for (std::size_t i = 0; i < constructor.parameters.size(); ++i) {
                    ValueTerm argument =
                        elaborate_value(expression.value_arguments[i], values, proofs, proof_order, absorbed);
                    require_known_type(constructor.parameters[i].type);
                    if (argument.kind != kind_of(constructor.parameters[i].type))
                        reject(expression.value_arguments[i].span,
                               "index argument `" + constructor.parameters[i].name + "` has the wrong value type");
                    indices.emplace(constructor.parameters[i].name, std::move(argument));
                }

                ProofEnvironment constructor_proofs;
                std::vector<std::string> constructor_proof_order;
                std::vector<z3::expr> no_absorbed;
                std::vector<std::string> proof_sources;
                for (std::size_t i = 0; i < constructor.proof_parameters.size(); ++i) {
                    auto const &parameter = constructor.proof_parameters[i];
                    SemanticProofType parameter_type = elaborate_proof_type(parameter.type, indices, constructor_proofs,
                                                                            constructor_proof_order, no_absorbed);
                    ProofEvidence argument = elaborate_any_proof(expression.proof_arguments[i], parameter.type,
                                                                 std::move(parameter_type), values, proofs, proof_order,
                                                                 absorbed, name + "." + parameter.name, run, {}, {});
                    proof_sources.push_back(print_proof(expression.proof_arguments[i]));
                    constructor_proofs.emplace(parameter.name, std::move(argument));
                    constructor_proof_order.push_back(parameter.name);
                }
                SemanticProofType result_type = elaborate_proof_type(
                    constructor.result_type, indices, constructor_proofs, constructor_proof_order, no_absorbed);
                auto inductive_result = std::get_if<InductiveType>(&result_type);
                if (!inductive_result || !same_type(context_, *inductive_result, expected))
                    reject(expression.span,
                           "proof constructor application `" + print_proof(expression) + "` has the wrong result type");

                if (rainfall_)
                    rainfall_->record(
                        "derive", "proof.inductive.constructor.apply", {"run:" + run}, "fine.proof-elaborator",
                        "A static indexed constructor forms proof evidence without creating a runtime datatype value",
                        {RainfallRecorder::string_field("family", expected.family),
                         RainfallRecorder::string_field("constructor", constructor.name),
                         RainfallRecorder::string_field("body", print_proof(expression)),
                         RainfallRecorder::raw_field("proof_arguments", RainfallRecorder::string_array(proof_sources)),
                         RainfallRecorder::boolean_field("runtime_value_created", false)});
                return {
                    std::move(name), std::move(expected), "constructor:" + constructor.name, expression.span, "", "",
                    expected_syntax.arguments};
            }

            ProofEvidence elaborate_proof_match(syntax::ProofExpr const &expression,
                                                syntax::ProofType const &expected_syntax,
                                                SemanticProofType expected, ValueEnvironment const &values,
                                                ProofEnvironment const &proofs,
                                                std::vector<std::string> const &proof_order,
                                                std::vector<z3::expr> const &absorbed, std::string name,
                                                std::string const &run) {
                auto scrutinee_found = proofs.find(expression.matched_proof);
                if (scrutinee_found == proofs.end())
                    reject(expression.span, "unknown proof `" + expression.matched_proof + "`");
                auto scrutinee = std::get_if<InductiveType>(&scrutinee_found->second.type);
                if (!scrutinee)
                    reject(expression.span, "proof match scrutinee `" + expression.matched_proof +
                                                "` is not indexed-family evidence");
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
                                         is_refinable_index(scrutinee_found->second.index_syntax[i],
                                                            scrutinee->indices[i]);
                        if (!refinable &&
                            !match_constructor_index(constructor.result_type.arguments[i],
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
                        std::string symbol = "fine.proof-match." + std::to_string(expression.node_id) + "." +
                                             constructor.name + "." + parameter.name;
                        constructor_values.emplace(parameter.name,
                                                   ValueTerm(kind, context_.constant(symbol.c_str(), sort(kind))));
                    }
                    ProofEnvironment no_proofs;
                    std::vector<std::string> no_proof_order;
                    std::vector<z3::expr> no_absorbed;
                    SemanticProofType constructor_result = elaborate_proof_type(
                        constructor.result_type, constructor_values, no_proofs, no_proof_order, no_absorbed);
                    auto result_indices = std::get_if<InductiveType>(&constructor_result);
                    if (!result_indices || result_indices->family != family.name)
                        throw std::logic_error("checked proof constructor changed family");

                    ValueEnvironment refined_values = values;
                    ValueEnvironment index_refinements;
                    for (std::size_t i = 0; i < scrutinee->indices.size(); ++i) {
                        bool refinable = i < scrutinee_found->second.index_syntax.size() &&
                                         is_refinable_index(scrutinee_found->second.index_syntax[i],
                                                            scrutinee->indices[i]);
                        if (!refinable) {
                            if (!same_ast(context_, result_indices->indices[i].expression,
                                          scrutinee->indices[i].expression)) {
                                possible = false;
                                break;
                            }
                            continue;
                        }
                        std::string const &index_name = scrutinee_found->second.index_syntax[i].name;
                        auto previous = index_refinements.find(index_name);
                        if (previous != index_refinements.end() &&
                            !same_ast(context_, previous->second.expression,
                                      result_indices->indices[i].expression)) {
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
                                          ReachableArm{&constructor, std::move(constructor_values),
                                                       std::move(refined_values), std::move(refined_indices)});
                }

                std::set<std::string> seen;
                for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
                    std::string const &constructor_name = expression.match_constructors[i];
                    auto global = proof_constructors_.find(constructor_name);
                    if (global == proof_constructors_.end() ||
                        global->second->result_type.name != family.name)
                        reject(expression.match_arm_spans[i], "proof constructor `" + constructor_name +
                                                                "` does not belong to `" + family.name + "`");
                    if (!seen.insert(constructor_name).second)
                        reject(expression.match_arm_spans[i], "duplicate proof match arm for `" +
                                                                constructor_name + "`");
                    auto arm_found = reachable.find(constructor_name);
                    if (arm_found == reachable.end())
                        reject(expression.match_arm_spans[i], "unreachable proof match arm `" + constructor_name +
                                                                "` must be omitted");
                    auto const &constructor = *arm_found->second.constructor;
                    if (expression.match_value_binders[i].size() != constructor.parameters.size())
                        reject(expression.match_arm_spans[i], "proof match arm `" + constructor_name + "` expects " +
                                                                std::to_string(constructor.parameters.size()) +
                                                                " value binders");
                    if (expression.match_proof_binders[i].size() != constructor.proof_parameters.size())
                        reject(expression.match_arm_spans[i], "proof match arm `" + constructor_name + "` expects " +
                                                                std::to_string(constructor.proof_parameters.size()) +
                                                                " evidence binders");

                    ValueEnvironment branch_values = arm_found->second.refined_values;
                    ProofEnvironment branch_proofs = proofs;
                    std::vector<std::string> branch_proof_order = proof_order;
                    std::vector<z3::expr> branch_absorbed = absorbed;
                    std::set<std::string> arm_names;
                    for (std::size_t j = 0; j < constructor.parameters.size(); ++j) {
                        std::string const &binder = expression.match_value_binders[i][j];
                        if (!arm_names.insert(binder).second || branch_values.contains(binder) ||
                            branch_proofs.contains(binder))
                            reject(expression.match_arm_spans[i], "duplicate proof match binder `" + binder + "`");
                        branch_values.emplace(binder,
                                              arm_found->second.constructor_values.at(constructor.parameters[j].name));
                    }

                    ProofEnvironment constructor_proofs;
                    std::vector<std::string> constructor_proof_order;
                    for (std::size_t j = 0; j < constructor.proof_parameters.size(); ++j) {
                        auto const &parameter = constructor.proof_parameters[j];
                        SemanticProofType field_type = elaborate_proof_type(
                            parameter.type, arm_found->second.constructor_values, constructor_proofs,
                            constructor_proof_order, branch_absorbed);
                        std::string const &binder = expression.match_proof_binders[i][j];
                        if (!arm_names.insert(binder).second || branch_values.contains(binder) ||
                            branch_proofs.contains(binder))
                            reject(expression.match_arm_spans[i], "duplicate proof match binder `" + binder + "`");
                        ProofEvidence field(binder, std::move(field_type),
                                            "proof-match-field:" + expression.matched_proof,
                                            expression.match_arm_spans[i], "", "", parameter.type.arguments);
                        auto field_inductive = std::get_if<InductiveType>(&field.type);
                        if (active_inductive_function_ && active_inductive_function_->induction_parameter &&
                            field_inductive && field_inductive->family == scrutinee->family) {
                            std::string const &root = *active_inductive_function_->induction_parameter;
                            if (expression.matched_proof == root ||
                                (scrutinee_found->second.structural_root &&
                                 *scrutinee_found->second.structural_root == root)) {
                                field.structural_root = root;
                                field.structural_parent = expression.matched_proof;
                            }
                        }
                        auto [inserted, ok] = branch_proofs.emplace(binder, std::move(field));
                        branch_proof_order.push_back(binder);
                        absorb(inserted->second, branch_absorbed,
                               {"proof-function:" + run, "proof-match:" + expression.matched_proof},
                               "proof-match-field");
                    }
                    SemanticProofType branch_expected = elaborate_proof_type(
                        expected_syntax, branch_values, branch_proofs, branch_proof_order, branch_absorbed);
                    (void)elaborate_any_proof(expression.match_bodies[i], expected_syntax,
                                              std::move(branch_expected), branch_values, branch_proofs,
                                              branch_proof_order, branch_absorbed, name + "." + constructor_name,
                                              run, {}, {});
                    if (rainfall_)
                        rainfall_->record(
                            "derive", "proof.inductive.match.branch",
                            {"proof-function:" + run, match_scope},
                            "fine.proof-elaborator",
                            "One reachable constructor owns its index refinements and bound static/proof fields",
                            {RainfallRecorder::string_field("constructor", constructor_name),
                             RainfallRecorder::raw_field(
                                 "refined_indices",
                                 RainfallRecorder::string_array(arm_found->second.refined_indices)),
                             RainfallRecorder::raw_field(
                                 "value_binders",
                                 RainfallRecorder::string_array(expression.match_value_binders[i])),
                             RainfallRecorder::raw_field(
                                 "proof_binders",
                                 RainfallRecorder::string_array(expression.match_proof_binders[i])),
                             RainfallRecorder::boolean_field("runtime_value_created", false)});
                }
                for (auto const &[constructor_name, arm] : reachable)
                    if (!seen.contains(constructor_name))
                        reject(expression.span, "non-exhaustive proof match: missing `" + constructor_name + "`");

                if (rainfall_)
                    rainfall_->record(
                        "derive", "proof.inductive.match", {"proof-function:" + run, match_scope},
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
                return {std::move(name), std::move(expected), "proof-match:" + expression.matched_proof,
                        expression.span,
                        expected_syntax.kind == syntax::ProofType::Kind::identity
                            ? print_value(expected_syntax.left)
                            : "",
                        expected_syntax.kind == syntax::ProofType::Kind::identity
                            ? print_value(expected_syntax.right)
                            : "",
                        std::move(index_syntax)};
            }

            ProofEvidence elaborate_any_proof(syntax::ProofExpr const &expression,
                                              syntax::ProofType const &expected_syntax, SemanticProofType expected,
                                              ValueEnvironment const &values, ProofEnvironment const &proofs,
                                              std::vector<std::string> const &proof_order,
                                              std::vector<z3::expr> const &absorbed, std::string name,
                                              std::string const &run, std::string const &proof_source,
                                              std::string const &type_source) {
                if (expression.kind == syntax::ProofExpr::Kind::match)
                    return elaborate_proof_match(expression, expected_syntax, std::move(expected), values, proofs,
                                                 proof_order, absorbed, std::move(name), run);
                if (auto identity = std::get_if<IdentityType>(&expected))
                    return elaborate_proof(expression, expected_syntax, std::move(*identity), values, proofs,
                                           proof_order, absorbed, std::move(name), run, proof_source, type_source);
                return elaborate_inductive_proof(expression, expected_syntax,
                                                 std::move(std::get<InductiveType>(expected)), values, proofs,
                                                 proof_order, absorbed, std::move(name), run);
            }

            void absorb(ProofEvidence const &proof, std::vector<z3::expr> &absorbed, std::vector<std::string> within,
                        std::string_view role, std::optional<std::string> source = std::nullopt) {
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

            void declare_proof_inductive(syntax::ProofInductiveDecl const &declaration) {
                if (declaration.name == "Id" || proof_inductives_.contains(declaration.name))
                    reject(declaration.span, "duplicate proof type `" + declaration.name + "`");
                std::set<std::string> index_names;
                for (auto const &index : declaration.indices) {
                    if (!index_names.insert(index.name).second)
                        reject(index.span, "duplicate proof index `" + index.name + "`");
                    require_known_type(index.type);
                }

                proof_inductives_.emplace(declaration.name, &declaration);
                std::set<std::string> local_constructors;
                for (auto const &constructor : declaration.constructors) {
                    if (!local_constructors.insert(constructor.name).second ||
                        proof_constructors_.contains(constructor.name) || proof_functions_.contains(constructor.name))
                        reject(constructor.span, "duplicate proof constructor `" + constructor.name + "`");
                    ValueEnvironment values;
                    ProofEnvironment proofs;
                    std::vector<std::string> proof_order;
                    std::vector<z3::expr> absorbed;
                    std::set<std::string> names;
                    for (auto const &parameter : constructor.parameters) {
                        if (!names.insert(parameter.name).second)
                            reject(parameter.span, "duplicate constructor parameter `" + parameter.name + "`");
                        require_known_type(parameter.type);
                        ValueKind kind = kind_of(parameter.type);
                        std::string symbol = "fine.proof-constructor." + constructor.name + "." + parameter.name;
                        values.emplace(parameter.name, ValueTerm(kind, context_.constant(symbol.c_str(), sort(kind))));
                    }
                    for (auto const &parameter : constructor.proof_parameters) {
                        if (!names.insert(parameter.name).second)
                            reject(parameter.span, "duplicate constructor parameter `" + parameter.name + "`");
                        SemanticProofType type =
                            elaborate_proof_type(parameter.type, values, proofs, proof_order, absorbed);
                        proofs.emplace(parameter.name,
                                       ProofEvidence(parameter.name, std::move(type), "proof-constructor-parameter",
                                                     parameter.span, "", ""));
                        proof_order.push_back(parameter.name);
                    }
                    SemanticProofType result =
                        elaborate_proof_type(constructor.result_type, values, proofs, proof_order, absorbed);
                    auto inductive = std::get_if<InductiveType>(&result);
                    if (!inductive || inductive->family != declaration.name)
                        reject(constructor.result_type.span, "proof constructor `" + constructor.name +
                                                                 "` must return `" + declaration.name + "(...)`");
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

            void declare_proof_function(syntax::ProofFunctionDecl const &declaration) {
                if (proof_functions_.contains(declaration.name) || functions_.contains(declaration.name) ||
                    constructors_.contains(declaration.name) || proof_constructors_.contains(declaration.name))
                    reject(declaration.span, "duplicate function `" + declaration.name + "`");
                ValueEnvironment indices;
                ProofEnvironment proofs;
                std::vector<std::string> proof_order;
                std::vector<z3::expr> absorbed;
                std::set<std::string> names;
                for (auto const &parameter : declaration.parameters) {
                    if (!names.insert(parameter.name).second)
                        reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
                    require_known_type(parameter.type);
                    ValueKind kind = kind_of(parameter.type);
                    std::string symbol = "fine.proof-function." + declaration.name + "." + parameter.name;
                    indices.emplace(parameter.name, ValueTerm(kind, context_.constant(symbol.c_str(), sort(kind))));
                }

                std::vector<std::string> parameter_sources;
                for (auto const &parameter : declaration.proof_parameters) {
                    if (!names.insert(parameter.name).second)
                        reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
                    SemanticProofType type =
                        elaborate_proof_type(parameter.type, indices, proofs, proof_order, absorbed);
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
                                           std::move(left_source), std::move(right_source),
                                           std::move(index_syntax));
                    if (rainfall_)
                        parameter_sources.push_back(rainfall_->source_node(
                            parameter.type.node_id, parameter.type.span,
                            parameter.type.kind == syntax::ProofType::Kind::identity ? "proof-type.identity"
                                                                                     : "proof-type.inductive"));
                    auto [inserted, ok] = proofs.emplace(parameter.name, std::move(evidence));
                    proof_order.push_back(parameter.name);
                    absorb(inserted->second, absorbed, {"proof-function:" + declaration.name},
                           "proof-function-parameter");
                }
                SemanticProofType result_type =
                    elaborate_proof_type(declaration.result_type, indices, proofs, proof_order, absorbed);
                std::optional<z3::expr> result_proposition;
                if (declaration.induction_parameter) {
                    if (!declaration.has_body)
                        reject(declaration.span, "`inducts` requires a body-bearing proof function");
                    auto parameter = proofs.find(*declaration.induction_parameter);
                    if (parameter == proofs.end())
                        reject(declaration.span, "unknown induction parameter `" +
                                                     *declaration.induction_parameter + "`");
                    if (!std::holds_alternative<InductiveType>(parameter->second.type))
                        reject(declaration.span, "induction parameter `" + *declaration.induction_parameter +
                                                     "` is not indexed-family evidence");
                    proof_functions_.emplace(declaration.name, &declaration);
                    active_inductive_function_ = &declaration;
                }
                if (declaration.has_body) {
                    (void)elaborate_any_proof(declaration.body, declaration.result_type, result_type, indices, proofs,
                                              proof_order, absorbed, declaration.name + ".result", declaration.name,
                                              {}, {});
                    active_inductive_function_ = nullptr;
                }
                else {
                    auto identity = std::get_if<IdentityType>(&result_type);
                    if (!identity)
                        reject(declaration.span, "proof function `" + declaration.name +
                                                     "` returning inductive evidence requires a body");
                    result_proposition.emplace(identity->left == identity->right);
                    z3::solver solver(context_);
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
                                           std::all_of(declaration.proof_parameters.begin(),
                                                       declaration.proof_parameters.end(),
                                                       [](syntax::CoeffectParameter const &parameter) {
                                                           return parameter.type.kind ==
                                                                  syntax::ProofType::Kind::identity;
                                                       });
                if (identity_searchable)
                    proof_function_order_.push_back(declaration.name);
                ++result_.proof_functions_verified;
                output_ << "verified proof function: " << declaration.name << '\n';
                if (rainfall_) {
                    std::string result_source = rainfall_->source_node(
                        declaration.result_type.node_id, declaration.result_type.span,
                        declaration.result_type.kind == syntax::ProofType::Kind::identity ? "proof-type.identity"
                                                                                          : "proof-type.inductive");
                    std::string proposition = result_proposition
                                                  ? rainfall_->term(*result_proposition,
                                                                    "proof-function-result-proposition")
                                                  : "";
                    rainfall_->record(
                        "transition", "proof.function.verify", {"proof-function:" + declaration.name},
                        "fine.proof-elaborator",
                        declaration.has_body
                            ? "A named proof-level function body constructs its static result under virtual parameters"
                            : "A named proof-level function result is refuted under its absorbed proof parameters",
                        {RainfallRecorder::string_field("function", declaration.name),
                         RainfallRecorder::raw_field("parameter_sources",
                                                     RainfallRecorder::string_array(parameter_sources)),
                         RainfallRecorder::string_field("result_source", result_source),
                         RainfallRecorder::string_field("result_proposition", proposition),
                         RainfallRecorder::string_field("status", declaration.has_body ? "body-checked" : "unsat"),
                         RainfallRecorder::boolean_field("runtime_function_created", false)});
                }
            }

            void declare_function(syntax::FunctionDecl const &declaration) {
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
                        source =
                            rainfall_->source_node(coeffect.type.node_id, coeffect.type.span, "proof-type.identity");
                        IdentityType const &identity = std::get<IdentityType>(evidence.type);
                        std::string proposition =
                            rainfall_->term(identity.left == identity.right, "coeffect-proposition");
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
                    reject(declaration.body.span, "function body does not have declared result type `" +
                                                      std::string(kind_name(result_kind)) + "`");
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
                        reject(declaration.span, "function `" + declaration.name +
                                                     "` does not satisfy its guarantees under declared coeffects");
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

            ValueTerm elaborate_call(syntax::ValueExpr const &expression, ValueEnvironment const &caller_values,
                                     ProofEnvironment const &caller_proofs,
                                     std::vector<std::string> const &caller_proof_order,
                                     std::vector<z3::expr> const &caller_absorbed) {
                auto found = functions_.find(expression.name);
                if (found == functions_.end()) {
                    if (proof_functions_.contains(expression.name))
                        reject(expression.span, "proof function `" + expression.name +
                                                    "` cannot be called from a runtime value expression");
                    if (proof_constructors_.contains(expression.name))
                        reject(expression.span, "proof constructor `" + expression.name +
                                                    "` cannot be called from a runtime value expression");
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
                        rainfall_->record(
                            "derive", "coeffect.demand.instantiate", {"call:" + expression.name},
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
                    reject(expression.span,
                           "call supplies unknown coeffect `" + explicit_arguments.begin()->first + "`");
                if (expression.using_proofs.empty() && !chosen.empty()) {
                    std::ostringstream insertion;
                    insertion << " using [";
                    for (std::size_t i = 0; i < chosen.size(); ++i) {
                        if (i)
                            insertion << ", ";
                        insertion << chosen[i].first << " = " << chosen[i].second;
                    }
                    insertion << ']';
                    request_materialization(syntax::ConcreteRange::empty_at(expression.call_argument_end),
                                            insertion.str(), expression.span);
                }
                ValueTerm result =
                    elaborate_value(function.body, callee_values, callee_proofs, callee_proof_order, callee_absorbed);
                require_known_type(function.result_type);
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
                    std::visit(
                        [&](auto const &item) {
                            execute_statement(item, run.name, assertion_index, values, proofs, proof_order, absorbed);
                        },
                        statement);
                }
                for (auto const &[range, text] : materializations_)
                    result_.materializations.push_back({{range.first, range.second}, text});
            }

            void execute_statement(syntax::LetDecl const &declaration, std::string const &run, std::size_t &,
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

            void execute_statement(syntax::ProofDecl const &declaration, std::string const &run, std::size_t &,
                                   ValueEnvironment &values, ProofEnvironment &proofs,
                                   std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed) {
                ensure_fresh(declaration.name, declaration.span, values, proofs);
                SemanticProofType expected =
                    elaborate_proof_type(declaration.type, values, proofs, proof_order, absorbed);
                std::string proof_source;
                std::string type_source;
                if (rainfall_) {
                    proof_source = rainfall_->source_node(declaration.value.node_id, declaration.value.span,
                                                          declaration.value.kind == syntax::ProofExpr::Kind::hole
                                                              ? "proof.expression.hole"
                                                              : "proof.expression");
                    type_source = rainfall_->source_node(declaration.type.node_id, declaration.type.span,
                                                         declaration.type.kind == syntax::ProofType::Kind::identity
                                                             ? "proof-type.identity"
                                                             : "proof-type.inductive");
                }
                ProofEvidence evidence =
                    elaborate_any_proof(declaration.value, declaration.type, std::move(expected), values, proofs,
                                        proof_order, absorbed, declaration.name, run, proof_source, type_source);
                if (rainfall_) {
                    if (auto identity = std::get_if<IdentityType>(&evidence.type)) {
                        std::string proposition =
                            rainfall_->term(identity->left == identity->right, "identity-proposition");
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
                    else {
                        rainfall_->record(
                            "object", "proof.inductive.form", {"run:" + run}, "fine.proof-elaborator",
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

            void execute_statement(syntax::AssertDecl const &declaration, std::string const &run,
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
                    rainfall_->source_term(declaration.proposition.node_id, declaration.proposition.span,
                                           "value.assertion", proposition.expression, "exact", {"run:" + run});
                    rainfall_->record("transition", "assert.verify", {"run:" + run}, "fine.two-level-elaborator",
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

    ExecutionResult execute(syntax::Document const &document, std::ostream &output, std::ostream *rainfall_output,
                            SourceSnapshot const *snapshot, std::string rainfall_run, ExecutionOptions options) {
        return Elaborator(output, rainfall_output, snapshot, std::move(rainfall_run), options).execute(document);
    }

    std::string apply_materializations(syntax::ConcreteSyntaxTree const &tree,
                                       std::vector<Materialization> materializations) {
        std::string source = tree.render();
        std::sort(materializations.begin(), materializations.end(), [](auto const &left, auto const &right) {
            return std::pair{left.range.begin, left.range.end} < std::pair{right.range.begin, right.range.end};
        });
        std::ostringstream output;
        std::size_t cursor = 0;
        for (auto const &materialization : materializations) {
            if (materialization.range.begin < cursor || materialization.range.begin > materialization.range.end ||
                materialization.range.end > source.size())
                throw std::runtime_error("invalid or overlapping source materialization");
            output << source.substr(cursor, materialization.range.begin - cursor) << materialization.text;
            cursor = materialization.range.end;
        }
        output << source.substr(cursor);
        return output.str();
    }

}  // namespace fine
