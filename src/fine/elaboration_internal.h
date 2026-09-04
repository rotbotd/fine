#pragma once

#include "runtime.h"

#include "proof_model_selector.h"
#include "rainfall.h"
#ifdef FINE_HAS_LIVE_LIFT
#include "live_lift.h"
#endif

#include "c++/z3++.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fine::elaboration {

    // Private semantic vocabulary shared by the independently compiled runtime
    // consumers. ProofEvidence deliberately remains disjoint from ValueTerm here.
    struct ValueKind {
        enum class Tag { integer, boolean, enumeration };

        Tag tag;
        std::string name;

        friend bool operator==(ValueKind const &, ValueKind const &) = default;
    };

    inline ValueKind integer_kind() {
        return {ValueKind::Tag::integer, "Int"};
    }

    inline ValueKind boolean_kind() {
        return {ValueKind::Tag::boolean, "Bool"};
    }

    inline std::string const &kind_name(ValueKind const &kind) {
        return kind.name;
    }

    inline ValueKind kind_of(syntax::ValueType const &type) {
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
        bool complete = true;

        ProofEvidence(std::string name, SemanticProofType type, std::string formation, syntax::SourceSpan span,
                      std::string left_source, std::string right_source,
                      std::vector<syntax::ValueExpr> index_syntax = {},
                      std::optional<std::string> structural_root = std::nullopt,
                      std::optional<std::string> structural_parent = std::nullopt, bool complete = true)
            : name(std::move(name)), type(std::move(type)), formation(std::move(formation)), span(span),
              left_source(std::move(left_source)), right_source(std::move(right_source)),
              index_syntax(std::move(index_syntax)), structural_root(std::move(structural_root)),
              structural_parent(std::move(structural_parent)), complete(complete) {}
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
        bool open = false;
        bool complete = true;
        std::size_t closed_frontier = 1;
        std::size_t open_leaves = 0;
    };

    struct IndexInstantiation {
        ValueEnvironment values;
        std::map<std::string, std::string> sources;
    };

    constexpr std::size_t max_proof_search_cost = 3;

    inline bool same_ast(z3::context &context, z3::expr const &left, z3::expr const &right) {
        return Z3_is_eq_ast(context, left, right);
    }

    inline bool same_type(z3::context &context, IdentityType const &left, IdentityType const &right) {
        return left.carrier == right.carrier && same_ast(context, left.left, right.left) &&
               same_ast(context, left.right, right.right);
    }

    inline bool same_type(z3::context &context, InductiveType const &left, InductiveType const &right) {
        if (left.family != right.family || left.indices.size() != right.indices.size())
            return false;
        for (std::size_t i = 0; i < left.indices.size(); ++i)
            if (left.indices[i].kind != right.indices[i].kind ||
                !same_ast(context, left.indices[i].expression, right.indices[i].expression))
                return false;
        return true;
    }

    inline bool same_type(z3::context &context, SemanticProofType const &left, SemanticProofType const &right) {
        if (left.index() != right.index())
            return false;
        if (auto identity = std::get_if<IdentityType>(&left))
            return same_type(context, *identity, std::get<IdentityType>(right));
        return same_type(context, std::get<InductiveType>(left), std::get<InductiveType>(right));
    }

    inline std::string print_value(syntax::ValueExpr const &expression) {
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

    inline std::string print_identity(syntax::ProofType const &type) {
        std::ostringstream result;
        result << "Id(" << kind_name(kind_of(type.carrier)) << ", " << print_value(type.left) << ", "
               << print_value(type.right) << ')';
        return result.str();
    }

    inline std::string print_proof_type(syntax::ProofType const &type) {
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

    inline std::string print_proof(syntax::ProofExpr const &expression) {
        switch (expression.kind) {
        case syntax::ProofExpr::Kind::name: return expression.name;
        case syntax::ProofExpr::Kind::reflexivity: return "refl(" + print_value(expression.value) + ")";
        case syntax::ProofExpr::Kind::application: {
            std::ostringstream result;
            result << expression.name << '(';
            std::size_t value_index = 0;
            std::size_t proof_index = 0;
            for (std::size_t i = 0; i < expression.argument_kinds.size(); ++i) {
                if (i)
                    result << ", ";
                if (expression.argument_kinds[i] == syntax::ProofExpr::ArgumentKind::value)
                    result << print_value(expression.value_arguments[value_index++]);
                else
                    result << print_proof(expression.proof_arguments[proof_index++]);
            }
            result << ')';
            if (!expression.using_coeffects.empty()) {
                result << " using [";
                for (std::size_t i = 0; i < expression.using_coeffects.size(); ++i) {
                    if (i)
                        result << ", ";
                    result << expression.using_coeffects[i] << " = " << print_proof(expression.using_proofs[i]);
                }
                result << ']';
            }
            return result.str();
        }
        case syntax::ProofExpr::Kind::match: {
            std::ostringstream result;
            result << "match " << expression.matched_proof << " { ";
            for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
                if (i)
                    result << ", ";
                result << expression.match_constructors[i] << '(';
                for (std::size_t j = 0; j < expression.match_binders[i].size(); ++j) {
                    if (j)
                        result << ", ";
                    result << expression.match_binders[i][j];
                }
                result << ") => " << print_proof(expression.match_bodies[i]);
            }
            return result.str() + " }";
        }
        case syntax::ProofExpr::Kind::hole: return "?";
        }
        return "<proof>";
    }

    inline std::string print_value_substituted(syntax::ValueExpr const &expression,
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

    inline std::string source_line(std::string_view source, std::size_t line_number) {
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

    [[noreturn]] inline void reject(syntax::SourceSpan span, std::string message) {
        throw SemanticError(span, std::move(message));
    }

    inline void ensure_fresh(std::string const &name, syntax::SourceSpan span, ValueEnvironment const &values,
                             ProofEnvironment const &proofs) {
        if (values.contains(name) || proofs.contains(name))
            reject(span, "duplicate local name `" + name + "`");
    }

    class MaterializationSink {
    public:
        virtual ~MaterializationSink() = default;
        virtual void request_materialization(syntax::ConcreteRange range, std::string text,
                                             syntax::SourceSpan span) = 0;
        virtual std::vector<Materialization> materializations_so_far() const = 0;
    };

    class ProofContext {
    public:
        virtual ~ProofContext() = default;
        virtual bool has_constructor(std::string const &name) const = 0;
        virtual bool has_function(std::string const &name) const = 0;
        virtual IdentityType elaborate_identity(syntax::ProofType const &type, ValueEnvironment const &values,
                                                ProofEnvironment const &proofs,
                                                std::vector<std::string> const &proof_order,
                                                std::vector<z3::expr> const &absorbed) = 0;
        virtual void absorb(ProofEvidence const &proof, std::vector<z3::expr> &absorbed,
                            std::vector<std::string> within, std::string_view role,
                            std::optional<std::string> source = std::nullopt) = 0;
    };

    class ValueElaborator {
    public:
        ValueElaborator(std::ostream &output, ExecutionOptions const &options, MaterializationSink &materializations);

        void set_rainfall(RainfallRecorder *rainfall) {
            rainfall_ = rainfall;
        }
        void connect_proofs(ProofContext &proofs) {
            proofs_ = &proofs;
        }
        z3::context &context() {
            return context_;
        }
        z3::context const &context() const {
            return context_;
        }
        bool has_constructor(std::string const &name) const {
            return constructors_.contains(name);
        }
        bool has_function(std::string const &name) const {
            return functions_.contains(name);
        }
        std::size_t functions_verified() const {
            return functions_verified_;
        }
        std::size_t coeffects_resolved() const {
            return coeffects_resolved_;
        }
        std::vector<std::string> runtime_kind_names() const;

        z3::sort sort(ValueKind const &kind);
        void require_known_type(syntax::ValueType const &type);
        void declare_enum(syntax::EnumDecl const &declaration);
        void record_boundary();
        ValueTerm elaborate_value(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                  ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                  std::vector<z3::expr> const &absorbed);
        bool match_constructor_index(syntax::ValueExpr const &pattern, z3::expr const &target,
                                     std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings);
        void declare_function(syntax::FunctionDecl const &declaration);

    private:
        z3::context context_;
        std::ostream &output_;
        ExecutionOptions const &options_;
        MaterializationSink &materializations_;
        RainfallRecorder *rainfall_ = nullptr;
        ProofContext *proofs_ = nullptr;
        std::map<std::string, std::unique_ptr<RuntimeEnum>> enums_;
        std::map<std::string, std::pair<RuntimeEnum *, std::size_t>> constructors_;
        std::map<std::string, syntax::FunctionDecl const *> functions_;
        std::size_t functions_verified_ = 0;
        std::size_t coeffects_resolved_ = 0;

        ValueTerm elaborate_constructor(syntax::ValueExpr const &expression, RuntimeEnum &enumeration,
                                        RuntimeConstructor const &constructor, ValueEnvironment const &values,
                                        ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                        std::vector<z3::expr> const &absorbed);
        ValueTerm elaborate_match(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                  ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                  std::vector<z3::expr> const &absorbed);
        ValueTerm elaborate_call(syntax::ValueExpr const &expression, ValueEnvironment const &caller_values,
                                 ProofEnvironment const &caller_proofs,
                                 std::vector<std::string> const &caller_proof_order,
                                 std::vector<z3::expr> const &caller_absorbed);
        bool contains_parameter(syntax::ValueExpr const &expression, std::map<std::string, ValueKind> const &parameters,
                                ValueEnvironment const &bindings);
    };

    class ProofEngine final : public ProofContext {
    public:
        ProofEngine(ValueElaborator &values, std::ostream &output, ExecutionOptions const &options,
                    MaterializationSink &materializations);

        void set_rainfall(RainfallRecorder *rainfall) {
            rainfall_ = rainfall;
        }
        bool has_constructor(std::string const &name) const override {
            return proof_constructors_.contains(name);
        }
        bool has_function(std::string const &name) const override {
            return proof_functions_.contains(name);
        }
        std::size_t functions_verified() const {
            return functions_verified_;
        }
        std::size_t holes_filled() const {
            return holes_filled_;
        }
        std::size_t holes_checkpointed() const {
            return holes_checkpointed_;
        }
        std::size_t coeffects_resolved() const {
            return coeffects_resolved_;
        }
        SemanticProofType elaborate_proof_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                               ProofEnvironment const &proofs,
                                               std::vector<std::string> const &proof_order,
                                               std::vector<z3::expr> const &absorbed);
        ProofEvidence elaborate_any_proof(syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax,
                                          SemanticProofType expected, ValueEnvironment const &values,
                                          ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                          std::vector<z3::expr> const &absorbed, std::string name,
                                          std::string const &run, std::string const &proof_source,
                                          std::string const &type_source);
        void absorb(ProofEvidence const &proof, std::vector<z3::expr> &absorbed, std::vector<std::string> within,
                    std::string_view role, std::optional<std::string> source = std::nullopt) override;
        void declare_proof_inductive(syntax::ProofInductiveDecl const &declaration);
        void declare_proof_function(syntax::ProofFunctionDecl const &declaration);

        IdentityType elaborate_identity(syntax::ProofType const &type, ValueEnvironment const &values,
                                        ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                        std::vector<z3::expr> const &absorbed) override;

    private:
        ValueElaborator &values_;
        std::ostream &output_;
        ExecutionOptions const &options_;
        MaterializationSink &materializations_;
        RainfallRecorder *rainfall_ = nullptr;
        std::map<std::string, syntax::ProofInductiveDecl const *> proof_inductives_;
        std::map<std::string, syntax::ProofConstructorDecl const *> proof_constructors_;
        std::map<std::string, syntax::ProofFunctionDecl const *> proof_functions_;
        std::vector<std::string> proof_function_order_;
        syntax::ProofFunctionDecl const *active_inductive_function_ = nullptr;
        std::size_t proof_model_index_ = 0;
        std::size_t functions_verified_ = 0;
        std::size_t holes_filled_ = 0;
        std::size_t holes_checkpointed_ = 0;
        std::size_t coeffects_resolved_ = 0;

        syntax::ValueExpr proof_syntax_as_value(syntax::ProofExpr const &expression);
        syntax::ValueExpr positional_value_argument(syntax::ProofExpr const &expression, std::size_t position);
        syntax::ProofExpr const &positional_proof_argument(syntax::ProofExpr const &expression, std::size_t position);
        InductiveType elaborate_inductive_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                               ProofEnvironment const &proofs,
                                               std::vector<std::string> const &proof_order,
                                               std::vector<z3::expr> const &absorbed);
        bool is_refinable_index(syntax::ValueExpr const &source, ValueTerm const &term);
        bool bind_result_index(syntax::ValueExpr const &pattern, z3::expr const &target,
                               std::string const &target_source, std::map<std::string, ValueKind> const &parameters,
                               ValueEnvironment &bindings, std::map<std::string, std::string> &source_bindings);
        bool value_pattern_ready(syntax::ValueExpr const &pattern, std::map<std::string, ValueKind> const &parameters,
                                 ValueEnvironment const &bindings);
        bool match_index_pattern(syntax::ValueExpr const &pattern, z3::expr const &target,
                                 std::string const &target_source, std::map<std::string, ValueKind> const &parameters,
                                 ValueEnvironment &bindings, std::map<std::string, std::string> &source_bindings,
                                 bool &added);
        bool match_identity_pattern(syntax::ProofType const &pattern, ProofEvidence const &target,
                                    std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings,
                                    std::map<std::string, std::string> &source_bindings, bool &added);
        bool complete_index_instantiation(syntax::ProofFunctionDecl const &function, IdentityType const &expected,
                                          IndexInstantiation const &instantiation);
        std::string index_instantiation_key(syntax::ProofFunctionDecl const &function,
                                            IndexInstantiation const &instantiation);
        std::vector<IndexInstantiation>
        infer_value_arguments(syntax::ProofFunctionDecl const &function, IdentityType const &expected,
                              std::string const &left_source, std::string const &right_source,
                              ProofEnvironment const &proofs, std::vector<std::string> const &proof_order);
        std::optional<IndexInstantiation> infer_inductive_value_arguments(syntax::ProofFunctionDecl const &function,
                                                                          syntax::ProofType const &expected_syntax,
                                                                          InductiveType const &expected);
        proof_model::Type proof_model_type(IdentityType const &type);
        std::string proof_model_type_key(proof_model::Type const &type);
        proof_model::Grammar
        make_direct_proof_model_grammar(std::string const &grammar_id, IdentityType const &expected,
                                        std::string const &left_source, std::string const &right_source,
                                        ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                        std::size_t budget);
        ProofEvidence elaborate_proof_application(syntax::ProofExpr const &expression,
                                                  syntax::ProofType const &expected_syntax, SemanticProofType expected,
                                                  ValueEnvironment const &values, ProofEnvironment const &proofs,
                                                  std::vector<std::string> const &proof_order,
                                                  std::vector<z3::expr> const &absorbed, std::string name,
                                                  std::string const &run);
        std::vector<ProofCandidate> enumerate_proof_candidates(
            syntax::ProofType const &expected_syntax, IdentityType const &expected, std::string const &left_source,
            std::string const &right_source, ValueEnvironment const &values, ProofEnvironment const &proofs,
            std::vector<std::string> const &proof_order, std::vector<z3::expr> const &absorbed, std::size_t budget);
        ProofEvidence elaborate_proof(syntax::ProofExpr const &expression, syntax::ProofType const &expected_syntax,
                                      IdentityType expected, ValueEnvironment const &values,
                                      ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                      std::vector<z3::expr> const &absorbed, std::string name, std::string const &run,
                                      std::string const &proof_source, std::string const &type_source);
        std::vector<ProofCandidate> enumerate_inductive_proof_candidates(syntax::ProofType const &expected_syntax,
                                                                         InductiveType const &expected,
                                                                         ProofEnvironment const &proofs,
                                                                         std::vector<std::string> const &proof_order);
        ProofEvidence elaborate_inductive_hole(syntax::ProofExpr const &expression,
                                               syntax::ProofType const &expected_syntax, InductiveType expected,
                                               ProofEnvironment const &proofs,
                                               std::vector<std::string> const &proof_order, std::string name,
                                               std::string const &run);
        ProofEvidence elaborate_inductive_proof(syntax::ProofExpr const &expression,
                                                syntax::ProofType const &expected_syntax, InductiveType expected,
                                                ValueEnvironment const &values, ProofEnvironment const &proofs,
                                                std::vector<std::string> const &proof_order,
                                                std::vector<z3::expr> const &absorbed, std::string name,
                                                std::string const &run);
        ProofEvidence elaborate_proof_match(syntax::ProofExpr const &expression,
                                            syntax::ProofType const &expected_syntax, SemanticProofType expected,
                                            ValueEnvironment const &values, ProofEnvironment const &proofs,
                                            std::vector<std::string> const &proof_order,
                                            std::vector<z3::expr> const &absorbed, std::string name,
                                            std::string const &run);
    };

    class DocumentRunner final : private MaterializationSink {
    public:
        DocumentRunner(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                       std::string rainfall_run, ExecutionOptions options);
        ExecutionResult execute(syntax::Document const &document);

    private:
        std::ostream &output_;
        ExecutionOptions options_;
        ExecutionResult result_;
        std::map<std::pair<std::size_t, std::size_t>, std::string> materializations_;
        ValueElaborator values_;
        std::optional<RainfallRecorder> rainfall_;
        ProofEngine proofs_;

        void request_materialization(syntax::ConcreteRange range, std::string text, syntax::SourceSpan span) override;
        std::vector<Materialization> materializations_so_far() const override;
        void execute_run(syntax::RunDecl const &run);
        void execute_statement(syntax::LetDecl const &declaration, std::string const &run, std::size_t &,
                               ValueEnvironment &values, ProofEnvironment &proofs,
                               std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed);
        void execute_statement(syntax::ProofDecl const &declaration, std::string const &run, std::size_t &,
                               ValueEnvironment &values, ProofEnvironment &proofs,
                               std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed);
        void execute_statement(syntax::AssertDecl const &declaration, std::string const &run,
                               std::size_t &assertion_index, ValueEnvironment &values, ProofEnvironment &proofs,
                               std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed);
    };

}  // namespace fine::elaboration
