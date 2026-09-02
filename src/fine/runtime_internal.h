#pragma once

#include "runtime.h"
#include "clause_observer.h"
#include "fixedpoint_observer.h"
#include "quantifier_observer.h"
#include "rainfall.h"
#include "synthesis.h"

#include "c++/z3++.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fine::runtime_detail {

    using TypePtr = std::shared_ptr<struct RuntimeType>;

    struct EnumInfo;
    struct DatatypeInfo;

    struct RuntimeType {
        enum class Kind { boolean, integer, enumeration, datatype, tuple, table };

        Kind kind;
        z3::sort sort;
        std::string display;
        std::vector<TypePtr> arguments;
        EnumInfo *enumeration = nullptr;
        DatatypeInfo *datatype = nullptr;
        std::unique_ptr<z3::func_decl> tuple_constructor;

        RuntimeType(Kind kind, z3::sort sort, std::string display)
            : kind(kind), sort(std::move(sort)), display(std::move(display)) {}
    };

    struct EnumInfo {
        std::string name;
        TypePtr type;
        std::vector<std::string> case_names;
        std::vector<z3::func_decl> constructors;
        std::vector<z3::expr> values;
    };

    struct DatatypeCaseInfo {
        std::string name;
        std::vector<std::string> field_names;
        std::vector<TypePtr> field_types;
        z3::func_decl constructor;
        z3::func_decl recognizer;
        std::vector<z3::func_decl> accessors;

        DatatypeCaseInfo(z3::context &context) : constructor(context), recognizer(context) {}
    };

    struct DatatypeInfo {
        std::string name;
        TypePtr type;
        std::vector<DatatypeCaseInfo> cases;
    };

    struct FunctionInfo {
        std::string name;
        std::vector<TypePtr> parameter_types;
        TypePtr result_type;
        z3::func_decl declaration;

        explicit FunctionInfo(z3::context &context) : declaration(context) {}
    };

    struct PredicateConstructorInfo {
        struct ArbitraryField {
            std::string binder;
            std::string view_name;
            z3::expr binder_term;
            z3::expr requirement;
            std::vector<z3::expr> premise_terms;
            std::vector<std::vector<z3::expr>> recursive_premise_indices;
            std::optional<z3::expr> availability_witness;

            explicit ArbitraryField(z3::context &context) : binder_term(context), requirement(context) {}
        };

        std::string name;
        std::vector<z3::expr> parameters;
        std::vector<z3::expr> result_indices;
        std::vector<z3::expr> premise_terms;
        std::vector<std::vector<z3::expr>> recursive_premise_indices;
        std::vector<ArbitraryField> arbitrary_fields;
        std::size_t premise_count = 0;
    };

    struct PredicateInfo {
        std::string name;
        std::vector<TypePtr> index_types;
        z3::func_decl relation;
        std::vector<PredicateConstructorInfo> constructors;
        bool horn_complete = true;

        explicit PredicateInfo(z3::context &context) : relation(context) {}
    };

    struct ViewInfo {
        std::string name;
        std::vector<std::string> parameter_names;
        std::vector<TypePtr> parameter_types;
        TypePtr carrier;
        std::vector<syntax::Expr> requirements;
        std::optional<syntax::Expr> witness;
    };

    struct Binding {
        TypePtr type;
        z3::expr value;
        bool is_model;
        syntax::SourceSpan span;
    };

    struct TypedExpression {
        TypePtr type;
        z3::expr expression;
    };

    struct SurfaceValue {
        enum class Kind { boolean, enumeration, datatype, tuple };

        Kind kind = Kind::boolean;
        bool boolean = false;
        EnumInfo *enumeration = nullptr;
        unsigned case_index = 0;
        std::vector<SurfaceValue> elements;
        DatatypeInfo *datatype = nullptr;
    };

    struct SurfaceEntry {
        SurfaceValue key;
        SurfaceValue value;
    };

    struct SurfaceTable {
        SurfaceValue default_value;
        std::vector<SurfaceEntry> entries;
    };

    struct AdmittedProof {
        std::string name;
        z3::expr theorem;
    };

    [[noreturn]] void reject(syntax::SourceSpan span, std::string message);

    class Runtime {
    public:
        explicit Runtime(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                         std::string rainfall_run);

        int execute(syntax::Document const &document);

    private:
        z3::context context_;
        std::ostream &output_;
        z3::fixedpoint fixedpoint_;
        std::unique_ptr<RainfallRecorder> rainfall_;
        TypePtr bool_type_;
        TypePtr int_type_;
        std::map<std::string, TypePtr> types_;
        std::map<std::string, std::unique_ptr<EnumInfo>> enums_;
        std::map<std::string, std::pair<EnumInfo *, unsigned>> cases_;
        std::map<std::string, std::unique_ptr<DatatypeInfo>> datatypes_;
        std::map<std::string, std::pair<DatatypeInfo *, unsigned>> constructors_;
        std::map<std::string, std::unique_ptr<FunctionInfo>> functions_;
        std::map<std::string, ViewInfo> views_;
        std::map<std::string, std::unique_ptr<PredicateInfo>> predicates_;
        std::map<std::string, Binding> bindings_;
        std::vector<AdmittedProof> admitted_proofs_;
        std::map<std::string, TypePtr> compound_types_;
        std::set<std::string> value_names_;
        unsigned tuple_sequence_ = 0;
        bool capture_source_edges_ = false;
        std::vector<std::string> source_edge_within_;

        static char const *expression_syntax_kind(syntax::Expr const &expression);

        static char const *expression_correspondence(syntax::Expr const &expression);

        TypedExpression completed_expression(syntax::Expr const &source, TypePtr type, z3::expr expression);

        void declare_expression_sources(syntax::Expr const &expression);

        void declare_table_sources(syntax::TableLiteral const &table);

        void declare_document_sources(syntax::Document const &document);

        void reserve_type_name(std::string const &name, syntax::SourceSpan span);

        void reserve_value_name(std::string const &name, syntax::SourceSpan span);

        void declare_enum(syntax::EnumDecl const &declaration);

        void declare_datatype(syntax::EnumDecl const &declaration);

        void declare_function(syntax::FunctionDecl const &declaration);

        TypePtr tuple_type(TypePtr const &first, TypePtr const &second, syntax::SourceSpan span);

        TypePtr resolve_type(syntax::Type const &syntax_type);

        void declare_view(syntax::ViewDecl const &declaration);

        z3::expr value(syntax::Expr const &expression, TypePtr const &expected);

        void declare_let(syntax::LetDecl const &declaration);

        z3::expr table_value(TypePtr const &type, syntax::TableLiteral const &literal);

        void declare_model(syntax::ModelDecl const &declaration);

        Binding const &binding(syntax::Expr const &expression, std::string const &role) const;

        static bool same(TypePtr const &left, TypePtr const &right);

        using ExpressionEnvironment = std::map<std::string, TypedExpression>;

        TypedExpression elaborate_expression(syntax::Expr const &expression, ExpressionEnvironment const &environment);

        void declare_predicate(syntax::PredicateDecl const &declaration);

        syntax::Expr lift_expression(z3::expr const &expression,
                                     std::vector<std::pair<std::string, z3::expr>> const &parameters) const;

        syntax::Expr lift_typed_expression(z3::expr const &expression, TypePtr const &type) const;

        static char const *operator_text(syntax::Expr::BinaryOp operation);

        static void print_expression(std::ostream &output, syntax::Expr const &expression);

        int execute_match_synthesis(syntax::SynthDecl const &declaration);

        int execute_synthesis(syntax::SynthDecl const &declaration);

        int execute_predicate_check(syntax::CheckDecl const &declaration);

        int execute_predicate_induction(syntax::CheckDecl const &declaration);

        int execute_predicate_invariant(syntax::CheckDecl const &declaration);

        int execute_check(syntax::CheckDecl const &declaration);

        static void expect_table(Binding const &binding, syntax::SourceSpan span, std::string const &role);

        static void expect_bool_range(Binding const &binding, syntax::SourceSpan span, std::string const &role);

        static std::map<std::string, syntax::Expr const *> take_map(syntax::SolveDecl const &solve);

        int execute_bisimulation(syntax::SolveDecl const &solve);

        static void validate_step(Binding const &step, TypePtr const &state, syntax::SourceSpan span,
                                  std::string const &role);

        static void validate_label(Binding const &label, TypePtr const &state, syntax::SourceSpan span,
                                   std::string const &role);

        SurfaceValue lift_value(TypePtr const &type, z3::expr const &expression) const;

        z3::expr reify_value(TypePtr const &type, SurfaceValue const &value);

        SurfaceTable lift_table(TypePtr const &type, z3::expr const &expression) const;

        void lift_table_into(TypePtr const &type, z3::expr const &expression, SurfaceTable &output) const;

        z3::expr reify_table(TypePtr const &type, SurfaceTable const &table);

        static void print_value(std::ostream &output, SurfaceValue const &value);

        static void print_table_expression(std::ostream &output, SurfaceTable const &table);

        static std::string render_model_witness(std::string const &name, TypePtr const &type,
                                                SurfaceTable const &table);
    };

}  // namespace fine::runtime_detail
