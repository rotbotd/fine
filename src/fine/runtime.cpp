#include "runtime.h"
#include "quantifier_observer.h"
#include "rainfall.h"
#include "synthesis.h"

#include "c++/z3++.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fine {
namespace {

using TypePtr = std::shared_ptr<struct RuntimeType>;

struct EnumInfo;

struct RuntimeType {
    enum class Kind { boolean, integer, enumeration, tuple, table };

    Kind kind;
    z3::sort sort;
    std::string display;
    std::vector<TypePtr> arguments;
    EnumInfo* enumeration = nullptr;
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
    enum class Kind { boolean, enumeration, tuple };

    Kind kind = Kind::boolean;
    bool boolean = false;
    EnumInfo* enumeration = nullptr;
    unsigned case_index = 0;
    std::vector<SurfaceValue> elements;
};

struct SurfaceEntry {
    SurfaceValue key;
    SurfaceValue value;
};

struct SurfaceTable {
    SurfaceValue default_value;
    std::vector<SurfaceEntry> entries;
};

[[noreturn]] void reject(syntax::SourceSpan span, std::string message) {
    throw SemanticError(span, std::move(message));
}

class Runtime {
public:
    explicit Runtime(std::ostream& output, std::ostream* rainfall_output)
        : output_(output),
          rainfall_(rainfall_output
                        ? std::make_unique<RainfallRecorder>(context_,
                                                            *rainfall_output)
                        : nullptr),
          bool_type_(std::make_shared<RuntimeType>(
                               RuntimeType::Kind::boolean,
                               context_.bool_sort(), "Bool")),
          int_type_(std::make_shared<RuntimeType>(
              RuntimeType::Kind::integer, context_.int_sort(), "Int")) {
        types_.emplace("Bool", bool_type_);
        types_.emplace("Int", int_type_);
    }

    int execute(syntax::Document const& document);

private:
    z3::context context_;
    std::ostream& output_;
    std::unique_ptr<RainfallRecorder> rainfall_;
    TypePtr bool_type_;
    TypePtr int_type_;
    std::map<std::string, TypePtr> types_;
    std::map<std::string, std::unique_ptr<EnumInfo>> enums_;
    std::map<std::string, std::pair<EnumInfo*, unsigned>> cases_;
    std::map<std::string, Binding> bindings_;
    std::map<std::string, TypePtr> compound_types_;
    std::set<std::string> value_names_;
    unsigned tuple_sequence_ = 0;

    void reserve_type_name(std::string const& name, syntax::SourceSpan span) {
        if (types_.contains(name)) reject(span, "type `" + name + "` is already declared");
    }

    void reserve_value_name(std::string const& name, syntax::SourceSpan span) {
        if (!value_names_.insert(name).second)
            reject(span, "value `" + name + "` is already declared");
    }

    void declare_enum(syntax::EnumDecl const& declaration) {
        reserve_type_name(declaration.name, declaration.span);
        std::set<std::string> local_cases;
        for (std::size_t i = 0; i < declaration.cases.size(); ++i) {
            std::string const& name = declaration.cases[i];
            if (!local_cases.insert(name).second)
                reject(declaration.case_spans[i], "duplicate enum case `" + name + "`");
            reserve_value_name(name, declaration.case_spans[i]);
        }

        std::vector<char const*> names;
        names.reserve(declaration.cases.size());
        for (std::string const& name : declaration.cases) names.push_back(name.c_str());
        z3::func_decl_vector constructors(context_);
        z3::func_decl_vector testers(context_);
        z3::sort sort = context_.enumeration_sort(
            declaration.name.c_str(), static_cast<unsigned>(names.size()),
            names.data(), constructors, testers);

        auto info = std::make_unique<EnumInfo>();
        info->name = declaration.name;
        info->case_names = declaration.cases;
        TypePtr type = std::make_shared<RuntimeType>(
            RuntimeType::Kind::enumeration, sort, declaration.name);
        info->type = type;
        type->enumeration = info.get();
        for (unsigned i = 0; i < constructors.size(); ++i) {
            info->constructors.push_back(constructors[i]);
            info->values.push_back(constructors[i]());
        }
        EnumInfo* stable = info.get();
        for (unsigned i = 0; i < stable->case_names.size(); ++i)
            cases_.emplace(stable->case_names[i], std::make_pair(stable, i));
        types_.emplace(declaration.name, type);
        enums_.emplace(declaration.name, std::move(info));
    }

    TypePtr resolve_type(syntax::Type const& syntax_type) {
        if (syntax_type.kind == syntax::Type::Kind::named) {
            auto found = types_.find(syntax_type.name);
            if (found == types_.end())
                reject(syntax_type.span, "unknown type `" + syntax_type.name + "`");
            return found->second;
        }

        if (syntax_type.arguments.size() != 2) {
            std::string noun = syntax_type.kind == syntax::Type::Kind::tuple
                                   ? "tuple" : "Table";
            reject(syntax_type.span, noun + " requires exactly two type arguments");
        }
        TypePtr first = resolve_type(syntax_type.arguments[0]);
        TypePtr second = resolve_type(syntax_type.arguments[1]);

        if (syntax_type.kind == syntax::Type::Kind::tuple) {
            if (first->kind == RuntimeType::Kind::table ||
                second->kind == RuntimeType::Kind::table)
                reject(syntax_type.span, "table values cannot be tuple components in Fine v1");
            std::string key = "(" + first->display + ", " + second->display + ")";
            auto found = compound_types_.find(key);
            if (found != compound_types_.end()) return found->second;

            char const* fields[] = {"first", "second"};
            z3::sort sorts[] = {first->sort, second->sort};
            z3::func_decl_vector projections(context_);
            std::string z3_name = "Fine.Pair." + std::to_string(tuple_sequence_++);
            z3::func_decl constructor = context_.tuple_sort(
                z3_name.c_str(), 2, fields, sorts, projections);
            TypePtr result = std::make_shared<RuntimeType>(
                RuntimeType::Kind::tuple, constructor.range(), key);
            result->arguments = {first, second};
            result->tuple_constructor = std::make_unique<z3::func_decl>(constructor);
            compound_types_.emplace(key, result);
            return result;
        }

        if (first->kind == RuntimeType::Kind::table ||
            second->kind == RuntimeType::Kind::table)
            reject(syntax_type.span, "nested tables are outside Fine v1");
        std::string key = "Table(" + first->display + ", " + second->display + ")";
        auto found = compound_types_.find(key);
        if (found != compound_types_.end()) return found->second;
        TypePtr result = std::make_shared<RuntimeType>(
            RuntimeType::Kind::table,
            context_.array_sort(first->sort, second->sort), key);
        result->arguments = {first, second};
        compound_types_.emplace(key, result);
        return result;
    }

    z3::expr value(syntax::Expr const& expression, TypePtr const& expected) {
        switch (expression.kind) {
        case syntax::Expr::Kind::boolean:
            if (expected->kind != RuntimeType::Kind::boolean)
                reject(expression.span, "Boolean value does not have type `" +
                                            expected->display + "`");
            return context_.bool_val(expression.boolean_value);
        case syntax::Expr::Kind::integer:
            if (expected->kind != RuntimeType::Kind::integer)
                reject(expression.span, "integer value does not have type `" +
                                            expected->display + "`");
            return context_.int_val(expression.integer_text.c_str());
        case syntax::Expr::Kind::name: {
            if (expected->kind != RuntimeType::Kind::enumeration)
                reject(expression.span, "name `" + expression.name +
                                            "` is not a value of type `" +
                                            expected->display + "`");
            auto found = cases_.find(expression.name);
            if (found == cases_.end())
                reject(expression.span, "unknown enum case `" + expression.name + "`");
            if (found->second.first != expected->enumeration)
                reject(expression.span, "enum case `" + expression.name +
                                            "` belongs to `" + found->second.first->name +
                                            "`, not `" + expected->display + "`");
            return found->second.first->values[found->second.second];
        }
        case syntax::Expr::Kind::tuple:
            if (expected->kind != RuntimeType::Kind::tuple)
                reject(expression.span, "tuple does not have type `" +
                                            expected->display + "`");
            if (expression.elements.size() != 2)
                reject(expression.span, "Fine v1 tuples contain exactly two values");
            return (*expected->tuple_constructor)(
                value(expression.elements[0], expected->arguments[0]),
                value(expression.elements[1], expected->arguments[1]));
        case syntax::Expr::Kind::binary:
        case syntax::Expr::Kind::conditional:
            reject(expression.span,
                   "computed expressions are not admitted as literal table cells");
        }
        reject(expression.span, "unknown Fine value form");
    }

    void declare_let(syntax::LetDecl const& declaration) {
        reserve_value_name(declaration.name, declaration.span);
        TypePtr type = resolve_type(declaration.type);
        if (type->kind != RuntimeType::Kind::table)
            reject(declaration.type.span, "a `let` binding in this slice must be a Table");
        z3::expr result = table_value(type, declaration.value);
        bindings_.emplace(declaration.name,
                          Binding{type, result, false, declaration.span});
    }

    z3::expr table_value(TypePtr const& type, syntax::TableLiteral const& literal) {
        TypePtr const& domain = type->arguments[0];
        TypePtr const& range = type->arguments[1];
        z3::expr result = z3::const_array(domain->sort,
                                          value(literal.default_value, range));
        std::vector<z3::expr> keys;
        for (syntax::TableEntry const& entry : literal.entries) {
            z3::expr key = value(entry.key, domain);
            for (z3::expr const& previous : keys) {
                if (Z3_is_eq_ast(context_, key, previous))
                    reject(entry.key.span, "duplicate table key");
            }
            keys.push_back(key);
            result = z3::store(result, key, value(entry.value, range));
        }
        return result;
    }

    void declare_model(syntax::ModelDecl const& declaration) {
        reserve_value_name(declaration.name, declaration.span);
        TypePtr type = resolve_type(declaration.type);
        if (type->kind != RuntimeType::Kind::table)
            reject(declaration.type.span, "a `model` hole must have Table type");
        z3::expr hole = declaration.value
                            ? table_value(type, *declaration.value)
                            : context_.constant(declaration.name.c_str(), type->sort);
        bindings_.emplace(declaration.name,
                          Binding{type, hole, !declaration.value, declaration.span});
    }

    Binding const& binding(syntax::Expr const& expression, std::string const& role) const {
        if (expression.kind != syntax::Expr::Kind::name)
            reject(expression.span, role + " must name a table declaration");
        auto found = bindings_.find(expression.name);
        if (found == bindings_.end())
            reject(expression.span, "unknown table `" + expression.name + "`");
        return found->second;
    }

    static bool same(TypePtr const& left, TypePtr const& right) {
        return left.get() == right.get();
    }

    using ExpressionEnvironment = std::map<std::string, TypedExpression>;

    TypedExpression elaborate_expression(
        syntax::Expr const& expression,
        ExpressionEnvironment const& environment) {
        switch (expression.kind) {
        case syntax::Expr::Kind::name: {
            auto found = environment.find(expression.name);
            if (found == environment.end())
                reject(expression.span, "unknown value `" + expression.name + "`");
            return found->second;
        }
        case syntax::Expr::Kind::boolean:
            return {bool_type_, context_.bool_val(expression.boolean_value)};
        case syntax::Expr::Kind::integer:
            return {int_type_, context_.int_val(expression.integer_text.c_str())};
        case syntax::Expr::Kind::tuple:
            reject(expression.span, "tuple expressions are not admitted in synth specs yet");
        case syntax::Expr::Kind::binary: {
            if (expression.elements.size() != 2)
                throw std::runtime_error("internal Fine binary expression arity");
            TypedExpression left = elaborate_expression(expression.elements[0], environment);
            TypedExpression right = elaborate_expression(expression.elements[1], environment);
            switch (expression.binary_op) {
            case syntax::Expr::BinaryOp::equal:
                if (!same(left.type, right.type) ||
                    left.type->kind == RuntimeType::Kind::table)
                    reject(expression.span, "both sides of `==` must have the same value type");
                return {bool_type_, left.expression == right.expression};
            case syntax::Expr::BinaryOp::greater_equal:
            case syntax::Expr::BinaryOp::less_equal:
                if (!same(left.type, int_type_) || !same(right.type, int_type_))
                    reject(expression.span, "ordered comparison requires two Int values");
                return {bool_type_, expression.binary_op ==
                                        syntax::Expr::BinaryOp::greater_equal
                                    ? left.expression >= right.expression
                                    : left.expression <= right.expression};
            case syntax::Expr::BinaryOp::logical_and:
            case syntax::Expr::BinaryOp::logical_or:
                if (!same(left.type, bool_type_) || !same(right.type, bool_type_))
                    reject(expression.span, "Boolean connective requires two Bool values");
                return {bool_type_, expression.binary_op ==
                                        syntax::Expr::BinaryOp::logical_and
                                    ? left.expression && right.expression
                                    : left.expression || right.expression};
            case syntax::Expr::BinaryOp::add:
            case syntax::Expr::BinaryOp::subtract:
                if (!same(left.type, int_type_) || !same(right.type, int_type_))
                    reject(expression.span, "integer arithmetic requires two Int values");
                return {int_type_, expression.binary_op == syntax::Expr::BinaryOp::add
                                       ? left.expression + right.expression
                                       : left.expression - right.expression};
            }
            throw std::runtime_error("internal Fine binary operator");
        }
        case syntax::Expr::Kind::conditional: {
            if (expression.elements.size() != 3)
                throw std::runtime_error("internal Fine conditional arity");
            TypedExpression condition =
                elaborate_expression(expression.elements[0], environment);
            TypedExpression yes = elaborate_expression(expression.elements[1], environment);
            TypedExpression no = elaborate_expression(expression.elements[2], environment);
            if (!same(condition.type, bool_type_))
                reject(expression.elements[0].span, "an `if` condition must have type Bool");
            if (!same(yes.type, no.type))
                reject(expression.span, "both `if` branches must have the same type");
            return {yes.type, z3::ite(condition.expression, yes.expression, no.expression)};
        }
        }
        throw std::runtime_error("internal Fine expression kind");
    }

    syntax::Expr lift_expression(
        z3::expr const& expression,
        std::vector<std::pair<std::string, z3::expr>> const& parameters) const {
        for (auto const& [name, parameter] : parameters) {
            if (Z3_is_eq_ast(context_, expression, parameter)) {
                syntax::Expr result;
                result.kind = syntax::Expr::Kind::name;
                result.name = name;
                return result;
            }
        }
        if (expression.is_true() || expression.is_false()) {
            syntax::Expr result;
            result.kind = syntax::Expr::Kind::boolean;
            result.boolean_value = expression.is_true();
            return result;
        }
        std::string numeral;
        if (expression.is_numeral(numeral)) {
            syntax::Expr result;
            result.kind = syntax::Expr::Kind::integer;
            result.integer_text = std::move(numeral);
            return result;
        }
        if (!expression.is_app())
            reject({}, "lift encountered a non-application synthesis term");

        Z3_decl_kind kind = expression.decl().decl_kind();
        if (kind == Z3_OP_ITE && expression.num_args() == 3) {
            syntax::Expr result;
            result.kind = syntax::Expr::Kind::conditional;
            result.elements.push_back(lift_expression(expression.arg(0), parameters));
            result.elements.push_back(lift_expression(expression.arg(1), parameters));
            result.elements.push_back(lift_expression(expression.arg(2), parameters));
            return result;
        }

        syntax::Expr::BinaryOp operation;
        switch (kind) {
        case Z3_OP_EQ: operation = syntax::Expr::BinaryOp::equal; break;
        case Z3_OP_GE: operation = syntax::Expr::BinaryOp::greater_equal; break;
        case Z3_OP_LE: operation = syntax::Expr::BinaryOp::less_equal; break;
        case Z3_OP_AND: operation = syntax::Expr::BinaryOp::logical_and; break;
        case Z3_OP_OR: operation = syntax::Expr::BinaryOp::logical_or; break;
        case Z3_OP_ADD: operation = syntax::Expr::BinaryOp::add; break;
        case Z3_OP_SUB: operation = syntax::Expr::BinaryOp::subtract; break;
        default:
            reject({}, "lift encountered an operation outside the admitted synth body");
        }
        if (expression.num_args() != 2)
            reject({}, "lift encountered a non-binary admitted operation");
        syntax::Expr result;
        result.kind = syntax::Expr::Kind::binary;
        result.binary_op = operation;
        result.elements.push_back(lift_expression(expression.arg(0), parameters));
        result.elements.push_back(lift_expression(expression.arg(1), parameters));
        return result;
    }

    static char const* operator_text(syntax::Expr::BinaryOp operation) {
        switch (operation) {
        case syntax::Expr::BinaryOp::equal: return "==";
        case syntax::Expr::BinaryOp::greater_equal: return ">=";
        case syntax::Expr::BinaryOp::less_equal: return "<=";
        case syntax::Expr::BinaryOp::logical_and: return "&&";
        case syntax::Expr::BinaryOp::logical_or: return "||";
        case syntax::Expr::BinaryOp::add: return "+";
        case syntax::Expr::BinaryOp::subtract: return "-";
        }
        return "?";
    }

    static void print_expression(std::ostream& output,
                                 syntax::Expr const& expression) {
        switch (expression.kind) {
        case syntax::Expr::Kind::name: output << expression.name; return;
        case syntax::Expr::Kind::boolean:
            output << (expression.boolean_value ? "true" : "false");
            return;
        case syntax::Expr::Kind::integer: output << expression.integer_text; return;
        case syntax::Expr::Kind::tuple:
            output << '(';
            for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                if (i) output << ", ";
                print_expression(output, expression.elements[i]);
            }
            output << ')';
            return;
        case syntax::Expr::Kind::binary:
            output << '(';
            print_expression(output, expression.elements[0]);
            output << ' ' << operator_text(expression.binary_op) << ' ';
            print_expression(output, expression.elements[1]);
            output << ')';
            return;
        case syntax::Expr::Kind::conditional:
            output << "if ";
            print_expression(output, expression.elements[0]);
            output << " { ";
            print_expression(output, expression.elements[1]);
            output << " } else { ";
            print_expression(output, expression.elements[2]);
            output << " }";
            return;
        }
    }

    int execute_synthesis(syntax::SynthDecl const& declaration) {
        reserve_value_name(declaration.name, declaration.span);
        TypePtr result_type = resolve_type(declaration.result_type);
        if (!same(result_type, int_type_))
            reject(declaration.result_type.span,
                   "the first synthesis slice returns Int");
        if (declaration.parameters.empty())
            reject(declaration.span, "a synthesized function needs a parameter");

        ExpressionEnvironment parameter_environment;
        std::vector<std::pair<std::string, z3::expr>> named_parameters;
        std::vector<z3::expr> parameters;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const& parameter = declaration.parameters[i];
            if (parameter.name == "result")
                reject(parameter.span, "`result` is reserved for the synthesis result");
            TypePtr type = resolve_type(parameter.type);
            if (!same(type, int_type_))
                reject(parameter.type.span,
                       "the first synthesis slice admits only Int parameters");
            std::string internal = "Fine.synth." + declaration.name + ".arg" +
                                   std::to_string(i);
            z3::expr value = context_.int_const(internal.c_str());
            if (!parameter_environment.emplace(
                    parameter.name, TypedExpression{type, value}).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            named_parameters.emplace_back(parameter.name, value);
            parameters.push_back(value);
        }

        std::string result_name = "Fine.synth." + declaration.name + ".result";
        z3::expr result = context_.int_const(result_name.c_str());
        ExpressionEnvironment specification_environment = parameter_environment;
        specification_environment.emplace("result", TypedExpression{int_type_, result});
        z3::expr specification = context_.bool_val(true);
        for (syntax::Expr const& condition : declaration.ensures) {
            TypedExpression elaborated =
                elaborate_expression(condition, specification_environment);
            if (!same(elaborated.type, bool_type_))
                reject(condition.span, "an ensured condition must have type Bool");
            specification = specification && elaborated.expression;
        }

        RefutationSynthesizer synthesizer(context_, declaration.name, parameters,
                                           result, specification,
                                           rainfall_.get());
        SynthesisResult synthesized = synthesizer.run();
        syntax::Expr lifted = lift_expression(synthesized.witness, named_parameters);
        std::ostringstream rendered;
        print_expression(rendered, lifted);
        std::string body = rendered.str();
        syntax::Expr reparsed = syntax::parse_expression(body);
        TypedExpression roundtrip =
            elaborate_expression(reparsed, parameter_environment);
        if (!same(roundtrip.type, result_type) ||
            !Z3_is_eq_ast(context_, roundtrip.expression, synthesized.witness))
            reject(declaration.span,
                   "parse(print(lift(witness))) violated exact AST identity");

        if (rainfall_) {
            rainfall_->record(
                "object", "fine.source-witness", {"synth:" + declaration.name},
                "fine.runtime",
                "Lifted, printed, parsed, elaborated witness with exact same-manager AST identity",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::string_field("body", body),
                 RainfallRecorder::string_field(
                     "semantic_term", rainfall_->term(synthesized.witness)),
                 RainfallRecorder::boolean_field("parse_reify_exact_identity", true)});
            rainfall_->record(
                "transition", "fine.witness.accept", {"synth:" + declaration.name},
                "fine.runtime",
                "Backend verification plus Fine source round-trip identity check",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::string_field("status", "source-program"),
                 RainfallRecorder::boolean_field("verified", true)});
            rainfall_->record(
                "scope", "synth.run.close", {"synth:" + declaration.name},
                "fine.runtime", "Native synthesis plus Fine source witness round trip",
                {RainfallRecorder::string_field("status", "source-program")});
        }

        output_ << "source-program: synthesized " << declaration.name << " from "
                << synthesized.selections.size() << " ground instances; core kept "
                << synthesized.core_indices.size() << '\n';
        output_ << body << '\n';
        output_ << "verification: no counterexample\n";
        output_ << "parse(print(lift(witness))): exact ast identity (diagnostic ast_id: "
                << Z3_get_ast_id(context_, synthesized.witness) << ")\n";
        return 0;
    }

    int execute_check(syntax::CheckDecl const& declaration) {
        reserve_value_name(declaration.name, declaration.span);
        if (declaration.parameters.empty())
            reject(declaration.span, "a check needs at least one parameter");

        struct CheckParameter {
            std::string name;
            TypePtr type;
            z3::expr term;
        };
        ExpressionEnvironment environment;
        std::vector<CheckParameter> parameters;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const& parameter = declaration.parameters[i];
            TypePtr type = resolve_type(parameter.type);
            if (!same(type, int_type_) && !same(type, bool_type_))
                reject(parameter.type.span,
                       "the first check slice admits only Int and Bool parameters");
            std::string internal = "Fine.check." + declaration.name + ".arg" +
                                   std::to_string(i);
            z3::expr term = context_.constant(internal.c_str(), type->sort);
            if (!environment.emplace(parameter.name,
                                     TypedExpression{type, term}).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            parameters.push_back({parameter.name, type, term});
        }

        auto conjunction = [&](std::vector<syntax::Expr> const& conditions,
                               std::string_view role) {
            z3::expr result = context_.bool_val(true);
            for (syntax::Expr const& condition : conditions) {
                TypedExpression elaborated =
                    elaborate_expression(condition, environment);
                if (!same(elaborated.type, bool_type_))
                    reject(condition.span, std::string(role) +
                                               " condition must have type Bool");
                result = result && elaborated.expression;
            }
            return result;
        };
        z3::expr assumptions = conjunction(declaration.assumes, "assumed");
        z3::expr guarantees = conjunction(declaration.ensures, "ensured");
        z3::expr counterexample_query = assumptions && !guarantees;
        std::string run_scope = "check:" + declaration.name;
        std::string query = "query:0";

        if (rainfall_) {
            rainfall_->record(
                "scope", "check.run.open", {run_scope}, "fine.check",
                "Fine check elaboration, one public counterexample query, and optional source-witness round trip; excludes Z3-internal search",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::number_field("parameters", parameters.size())});
            rainfall_->record(
                "constraint", "check.counterexample.assert", {run_scope},
                "fine.check",
                "Conjunction of source assumptions and the negation of all source guarantees",
                {RainfallRecorder::string_field(
                     "assumptions", rainfall_->term(assumptions)),
                 RainfallRecorder::string_field(
                     "guarantees", rainfall_->term(guarantees)),
                 RainfallRecorder::string_field(
                     "assertion", rainfall_->term(counterexample_query))});
            rainfall_->record(
                "scope", "solver.query.open", {run_scope, query},
                "fine.check", "Public solver assertion boundary",
                {RainfallRecorder::string_field("id", query),
                 RainfallRecorder::string_field(
                     "purpose", "find a source-level counterexample"),
                 RainfallRecorder::string_field(
                     "assertion", rainfall_->term(counterexample_query)),
                 RainfallRecorder::string_field(
                     "polarity", "counterexample-exists")});
        }

        z3::solver solver(context_);
        solver.add(counterexample_query);
        z3::check_result result = solver.check();
        if (rainfall_) {
            char const* status = result == z3::sat
                                     ? "sat"
                                     : result == z3::unsat ? "unsat" : "unknown";
            rainfall_->record(
                "transition", "solver.query.result", {run_scope, query},
                "z3.public-api",
                "Final public check result only; no claim about solver search or internal cause",
                {RainfallRecorder::string_field("query", query),
                 RainfallRecorder::string_field("status", status),
                 RainfallRecorder::string_field(
                     "polarity", "counterexample-exists"),
                 RainfallRecorder::string_field(
                     "domain_outcome", result == z3::sat ? "refuted" :
                                           result == z3::unsat ? "verified" :
                                                               "unknown")});
            rainfall_->record(
                "scope", "solver.query.close", {run_scope, query},
                "fine.check", "Public solver query lifetime",
                {RainfallRecorder::string_field("id", query)});
        }
        if (result == z3::unknown)
            reject(declaration.span,
                   "counterexample query was unknown: " + solver.reason_unknown());
        if (result == z3::unsat) {
            if (rainfall_)
                rainfall_->record(
                    "scope", "check.run.close", {run_scope}, "fine.check",
                    "Source check completed with no counterexample",
                    {RainfallRecorder::string_field("status", "verified")});
            output_ << "verified: " << declaration.name << '\n';
            output_ << "counterexample: none\n";
            return 0;
        }

        z3::model model = solver.get_model();
        std::vector<z3::expr> values;
        values.reserve(parameters.size());
        std::ostringstream rendered;
        rendered << "counterexample " << declaration.name << " {\n";
        for (CheckParameter const& parameter : parameters) {
            z3::expr value = model.eval(parameter.term, true);
            syntax::Expr lifted = lift_expression(value, {});
            values.push_back(value);
            rendered << "  " << parameter.name << ": " << parameter.type->display
                     << " = ";
            print_expression(rendered, lifted);
            rendered << ";\n";
            if (rainfall_)
                rainfall_->record(
                    "derive", "model.eval-assignment", {run_scope},
                    "z3.public-api",
                    "Completed evaluation of one check parameter under the returned counterexample model",
                    {RainfallRecorder::string_field("evidence_query", query),
                     RainfallRecorder::string_field("parameter", parameter.name),
                     RainfallRecorder::string_field(
                         "term", rainfall_->term(parameter.term)),
                     RainfallRecorder::string_field("value", rainfall_->term(value)),
                     RainfallRecorder::boolean_field("model_completion", true),
                     RainfallRecorder::string_field(
                         "relation", "equality-under-this-model")});
        }
        rendered << "}\n";
        std::string witness_source = rendered.str();

        syntax::Document witness_document = syntax::parse(witness_source);
        if (witness_document.declarations.size() != 1)
            throw std::runtime_error("internal counterexample parser returned extra declarations");
        auto const* witness = std::get_if<syntax::CounterexampleDecl>(
            &witness_document.declarations.front());
        if (!witness || witness->name != declaration.name ||
            witness->entries.size() != parameters.size())
            throw std::runtime_error("internal counterexample parser changed the witness");
        ExpressionEnvironment empty_environment;
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            syntax::CounterexampleEntry const& entry = witness->entries[i];
            CheckParameter const& parameter = parameters[i];
            if (entry.name != parameter.name)
                throw std::runtime_error("counterexample parser changed a parameter name");
            TypePtr parsed_type = resolve_type(entry.type);
            TypedExpression roundtrip =
                elaborate_expression(entry.value, empty_environment);
            if (!same(parsed_type, parameter.type) ||
                !same(roundtrip.type, parameter.type) ||
                !Z3_is_eq_ast(context_, roundtrip.expression, values[i]))
                reject(entry.span,
                       "parse(print(lift(value))) violated exact AST identity");
        }

        if (rainfall_) {
            rainfall_->record(
                "object", "fine.counterexample-witness", {run_scope},
                "fine.runtime",
                "Lifted, printed, parsed, and elaborated primitive assignments with exact same-manager AST identity",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::string_field("source", witness_source),
                 RainfallRecorder::boolean_field(
                     "parse_reify_exact_identity", true)});
            rainfall_->record(
                "transition", "fine.witness.accept", {run_scope}, "fine.runtime",
                "Satisfiable counterexample query plus Fine source round-trip identity check",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::string_field("evidence_query", query),
                 RainfallRecorder::string_field(
                     "status", "counterexample-witness"),
                 RainfallRecorder::boolean_field(
                     "source_roundtrip_exact_identity", true)});
            rainfall_->record(
                "scope", "check.run.close", {run_scope}, "fine.runtime",
                "Source check completed with a returned counterexample",
                {RainfallRecorder::string_field(
                     "status", "counterexample-witness")});
        }

        output_ << "refuted: " << declaration.name << '\n';
        output_ << witness_source;
        output_ << "parse(print(lift(values))): exact ast identity\n";
        return 0;
    }

    static void expect_table(Binding const& binding, syntax::SourceSpan span,
                             std::string const& role) {
        if (binding.type->kind != RuntimeType::Kind::table)
            reject(span, role + " must have Table type");
    }

    static void expect_bool_range(Binding const& binding, syntax::SourceSpan span,
                                  std::string const& role) {
        expect_table(binding, span, role);
        if (binding.type->arguments[1]->kind != RuntimeType::Kind::boolean)
            reject(span, role + " must return Bool");
    }

    static std::map<std::string, syntax::Expr const*>
    take_map(syntax::ProofDecl const& proof) {
        std::map<std::string, syntax::Expr const*> result;
        for (syntax::NamedArgument const& argument : proof.takes) {
            if (!result.emplace(argument.name, &argument.value).second)
                reject(argument.span, "duplicate proof input `" + argument.name + "`");
        }
        static std::set<std::string> const expected{
            "relation", "left_step", "right_step",
            "left_label", "right_label", "initial"};
        for (auto const& [name, expression] : result) {
            (void)expression;
            if (!expected.contains(name))
                reject(proof.span, "unexpected proof input `" + name + "`");
        }
        for (std::string const& name : expected) {
            if (!result.contains(name))
                reject(proof.span, "missing proof input `" + name + "`");
        }
        return result;
    }

    int execute_bisimulation(syntax::ProofDecl const& proof) {
        if (proof.name != "bisimulation")
            reject(proof.span, "unknown proof form `" + proof.name +
                                   "`; this slice admits `proof bisimulation`");
        auto inputs = take_map(proof);
        Binding const& relation = binding(*inputs.at("relation"), "relation");
        Binding const& left_step = binding(*inputs.at("left_step"), "left_step");
        Binding const& right_step = binding(*inputs.at("right_step"), "right_step");
        Binding const& left_label = binding(*inputs.at("left_label"), "left_label");
        Binding const& right_label = binding(*inputs.at("right_label"), "right_label");

        expect_bool_range(relation, proof.span, "relation");
        if (!relation.is_model)
            reject(inputs.at("relation")->span, "relation must name the `model` hole");
        TypePtr relation_domain = relation.type->arguments[0];
        if (relation_domain->kind != RuntimeType::Kind::tuple)
            reject(inputs.at("relation")->span,
                   "relation must be indexed by a pair of enum states");
        TypePtr left_type = relation_domain->arguments[0];
        TypePtr right_type = relation_domain->arguments[1];
        if (left_type->kind != RuntimeType::Kind::enumeration ||
            right_type->kind != RuntimeType::Kind::enumeration)
            reject(inputs.at("relation")->span,
                   "bisimulation state types must be finite enums");

        validate_step(left_step, left_type, inputs.at("left_step")->span, "left_step");
        validate_step(right_step, right_type, inputs.at("right_step")->span, "right_step");
        validate_label(left_label, left_type, inputs.at("left_label")->span, "left_label");
        validate_label(right_label, right_type, inputs.at("right_label")->span, "right_label");

        if (proof.gives.kind != syntax::Expr::Kind::name ||
            proof.gives.name != inputs.at("relation")->name)
            reject(proof.gives.span, "`gives` must return the relation model hole");
        z3::expr initial = value(*inputs.at("initial"), relation_domain);

        std::string run_scope = "bisim:" + inputs.at("relation")->name;
        if (rainfall_) {
            rainfall_->record(
                "scope", "bisim.run.open", {run_scope}, "fine.bisimulation",
                "Fine's finite bisimulation elaboration, one public solver query, model extensionalization, and source round trip; excludes Z3-internal search",
                {RainfallRecorder::string_field(
                     "relation", rainfall_->term(relation.value)),
                 RainfallRecorder::string_field(
                     "left_step", rainfall_->term(left_step.value)),
                 RainfallRecorder::string_field(
                     "right_step", rainfall_->term(right_step.value)),
                 RainfallRecorder::string_field(
                     "left_label", rainfall_->term(left_label.value)),
                 RainfallRecorder::string_field(
                     "right_label", rainfall_->term(right_label.value)),
                 RainfallRecorder::string_field(
                     "initial", rainfall_->term(initial)),
                 RainfallRecorder::boolean_field("mbqi", true)});
        }

        z3::expr left = context_.constant("Fine.left", left_type->sort);
        z3::expr right = context_.constant("Fine.right", right_type->sort);
        z3::expr left_next = context_.constant("Fine.left_next", left_type->sort);
        z3::expr right_next = context_.constant("Fine.right_next", right_type->sort);

        auto tuple = [](TypePtr const& type, z3::expr const& first,
                        z3::expr const& second) {
            return (*type->tuple_constructor)(first, second);
        };
        auto related = [&](z3::expr const& l, z3::expr const& r) {
            return z3::select(relation.value, tuple(relation_domain, l, r));
        };
        TypePtr left_step_domain = left_step.type->arguments[0];
        TypePtr right_step_domain = right_step.type->arguments[0];
        auto steps_left = [&](z3::expr const& from, z3::expr const& to) {
            return z3::select(left_step.value, tuple(left_step_domain, from, to));
        };
        auto steps_right = [&](z3::expr const& from, z3::expr const& to) {
            return z3::select(right_step.value, tuple(right_step_domain, from, to));
        };

        auto named_forall = [&](char const* role,
                                std::vector<z3::expr> const& variables,
                                z3::expr const& body) {
            std::vector<Z3_app> bound;
            bound.reserve(variables.size());
            for (z3::expr const& variable : variables)
                bound.push_back(reinterpret_cast<Z3_app>(
                    static_cast<Z3_ast>(variable)));
            std::string qid = std::string("fine.bisim.") + role;
            Z3_ast result = Z3_mk_quantifier_const_ex(
                context_, true, 0, context_.str_symbol(qid.c_str()),
                context_.str_symbol(""), static_cast<unsigned>(bound.size()),
                bound.data(), 0, nullptr, 0, nullptr, body);
            context_.check_error();
            return z3::expr(context_, result);
        };

        std::vector<std::pair<std::string, z3::expr>> assertions;
        assertions.emplace_back(
            "labels-agree",
            named_forall("labels-agree", {left, right},
                z3::implies(related(left, right),
                    z3::select(left_label.value, left) ==
                    z3::select(right_label.value, right))));
        assertions.emplace_back(
            "left-step-matched",
            named_forall("left-step-matched", {left, right, left_next},
                z3::implies(related(left, right) && steps_left(left, left_next),
                    z3::exists(right_next,
                        steps_right(right, right_next) &&
                        related(left_next, right_next)))));
        assertions.emplace_back(
            "right-step-matched",
            named_forall("right-step-matched", {left, right, right_next},
                z3::implies(related(left, right) &&
                                steps_right(right, right_next),
                    z3::exists(left_next,
                        steps_left(left, left_next) &&
                        related(left_next, right_next)))));
        assertions.emplace_back("initial-related",
                                z3::select(relation.value, initial));

        z3::solver solver(context_);
        z3::params parameters(context_);
        parameters.set("mbqi", true);
        parameters.set("ematching", false);
        solver.set(parameters);
        std::vector<std::string> assertion_references;
        for (auto const& [role, assertion] : assertions) {
            solver.add(assertion);
            if (rainfall_) {
                std::string reference = rainfall_->term(assertion);
                assertion_references.push_back(reference);
                rainfall_->record(
                    "constraint", "bisim.clause.assert", {run_scope},
                    "fine.bisimulation",
                    "Fully elaborated bisimulation clause asserted through Z3's public solver API",
                    {RainfallRecorder::string_field("role", role),
                     RainfallRecorder::string_field("assertion", reference)});
            }
        }

        std::string query = "query:0";
        if (rainfall_) {
            rainfall_->record(
                "scope", "solver.query.open", {run_scope, query},
                "fine.bisimulation", "Public solver assertion boundary",
                {RainfallRecorder::string_field("id", query),
                 RainfallRecorder::string_field(
                     "purpose", "find a finite bisimulation relation model"),
                 RainfallRecorder::raw_field(
                     "assertions",
                     RainfallRecorder::string_array(assertion_references)),
                 RainfallRecorder::string_field("polarity", "model-exists"),
                 RainfallRecorder::boolean_field("mbqi", true),
                 RainfallRecorder::boolean_field("ematching", false)});
        }

        std::unique_ptr<RainfallQuantifierObserver> quantifier_observer;
        if (rainfall_) {
            quantifier_observer = std::make_unique<RainfallQuantifierObserver>(
                solver, *rainfall_, std::vector<std::string>{run_scope, query},
                false);
        }
        z3::check_result result = solver.check();
        if (rainfall_) {
            char const* status = result == z3::sat
                                     ? "sat"
                                     : result == z3::unsat ? "unsat" : "unknown";
            rainfall_->record(
                "transition", "solver.query.result", {run_scope, query},
                "z3.public-api",
                "Final public check result only; no claim about solver search, MBQI steps, or internal cause",
                {RainfallRecorder::string_field("query", query),
                 RainfallRecorder::string_field("status", status),
                 RainfallRecorder::string_field("polarity", "model-exists")});
            rainfall_->record(
                "scope", "solver.query.close", {run_scope, query},
                "fine.bisimulation", "Public solver query lifetime",
                {RainfallRecorder::string_field("id", query)});
        }
        if (result != z3::sat) {
            std::string detail = result == z3::unknown
                                     ? "unknown: " + solver.reason_unknown()
                                     : "unsatisfiable";
            reject(proof.span, "bisimulation model hole was " + detail);
        }

        z3::model model = solver.get_model();
        z3::expr canonical = z3::const_array(relation_domain->sort, context_.bool_val(false));
        std::string cell_evidence = "[";
        bool first_cell = true;
        for (z3::expr const& l : left_type->enumeration->values) {
            for (z3::expr const& r : right_type->enumeration->values) {
                z3::expr key = tuple(relation_domain, l, r);
                z3::expr selection = z3::select(relation.value, key);
                z3::expr cell = model.eval(selection, true);
                if (!cell.is_true() && !cell.is_false())
                    reject(proof.span, "model returned a non-Boolean relation cell");
                if (rainfall_) {
                    std::string key_reference = rainfall_->term(key);
                    std::string selection_reference = rainfall_->term(selection);
                    std::string value_reference = rainfall_->term(cell);
                    rainfall_->record(
                        "derive", "model.eval-cell", {run_scope},
                        "z3.public-api",
                        "Completed evaluation of one finite relation selection under the model returned by the named satisfiable query",
                        {RainfallRecorder::string_field("evidence_query", query),
                         RainfallRecorder::string_field("key", key_reference),
                         RainfallRecorder::string_field(
                             "selection", selection_reference),
                         RainfallRecorder::string_field("value", value_reference),
                         RainfallRecorder::boolean_field("model_completion", true),
                         RainfallRecorder::string_field(
                             "relation", "equality-under-this-model")});
                    if (!first_cell) cell_evidence += ',';
                    first_cell = false;
                    cell_evidence +=
                        "{\"key\":" + RainfallRecorder::quote(key_reference) +
                        ",\"selection\":" +
                        RainfallRecorder::quote(selection_reference) +
                        ",\"value\":" +
                        RainfallRecorder::quote(value_reference) + "}";
                }
                if (cell.is_true())
                    canonical = z3::store(canonical, key, context_.bool_val(true));
            }
        }
        cell_evidence += ']';
        if (rainfall_) {
            rainfall_->record(
                "derive", "bisim.extensionalize-model", {run_scope},
                "fine.bisimulation",
                "Complete finite-domain enumeration assembled into Fine's deterministic false-default array plus true stores",
                {RainfallRecorder::string_field("evidence_query", query),
                 RainfallRecorder::raw_field("cells", cell_evidence),
                 RainfallRecorder::string_field(
                     "output", rainfall_->term(canonical)),
                 RainfallRecorder::string_field(
                     "policy", "false-default-then-enumeration-order-true-stores")});
        }

        SurfaceTable lifted = lift_table(relation.type, canonical);
        std::string witness_source = render_model_witness(
            inputs.at("relation")->name, relation.type, lifted);
        syntax::Document witness_document = syntax::parse(witness_source);
        if (witness_document.declarations.size() != 1)
            throw std::runtime_error("internal Fine witness parser returned extra declarations");
        auto const* witness = std::get_if<syntax::ModelDecl>(
            &witness_document.declarations.front());
        if (!witness || !witness->value || witness->name != inputs.at("relation")->name)
            throw std::runtime_error("internal Fine witness parser changed the declaration");
        TypePtr parsed_type = resolve_type(witness->type);
        if (!same(parsed_type, relation.type))
            throw std::runtime_error("internal Fine witness parser changed the model type");
        z3::expr roundtrip = table_value(parsed_type, *witness->value);
        if (!Z3_is_eq_ast(context_, canonical, roundtrip))
            reject(proof.span,
                   "parse(print(lift(x))) violated exact AST identity after reification");

        if (rainfall_) {
            rainfall_->record(
                "object", "fine.model-witness", {run_scope}, "fine.runtime",
                "Lifted, printed, parsed, and elaborated model witness with exact same-manager AST identity",
                {RainfallRecorder::string_field(
                     "declaration", inputs.at("relation")->name),
                 RainfallRecorder::string_field("source", witness_source),
                 RainfallRecorder::string_field(
                     "semantic_term", rainfall_->term(canonical)),
                 RainfallRecorder::boolean_field(
                     "parse_reify_exact_identity", true)});
            rainfall_->record(
                "transition", "fine.witness.accept", {run_scope}, "fine.runtime",
                "Satisfiable model query plus finite extensionalization and Fine source round-trip identity check",
                {RainfallRecorder::string_field(
                     "declaration", inputs.at("relation")->name),
                 RainfallRecorder::string_field("evidence_query", query),
                 RainfallRecorder::string_field("status", "model-witness"),
                 RainfallRecorder::boolean_field(
                     "source_roundtrip_exact_identity", true)});
            rainfall_->record(
                "scope", "bisim.run.close", {run_scope}, "fine.runtime",
                "Finite bisimulation model and Fine source witness round trip",
                {RainfallRecorder::string_field("status", "model-witness")});
        }

        output_ << "sat: z3 filled model-shaped hole "
                << inputs.at("relation")->name << '\n';
        output_ << witness_source;
        output_ << "parse(print(lift(x))): exact ast identity (diagnostic ast_id: "
                << Z3_get_ast_id(context_, canonical) << ")\n";
        return 0;
    }

    static void validate_step(Binding const& step, TypePtr const& state,
                              syntax::SourceSpan span, std::string const& role) {
        expect_bool_range(step, span, role);
        TypePtr domain = step.type->arguments[0];
        if (domain->kind != RuntimeType::Kind::tuple ||
            !same(domain->arguments[0], state) ||
            !same(domain->arguments[1], state))
            reject(span, role + " must have type Table((" + state->display + ", " +
                             state->display + "), Bool)");
    }

    static void validate_label(Binding const& label, TypePtr const& state,
                               syntax::SourceSpan span, std::string const& role) {
        expect_bool_range(label, span, role);
        if (!same(label.type->arguments[0], state))
            reject(span, role + " must have type Table(" + state->display + ", Bool)");
    }

    SurfaceValue lift_value(TypePtr const& type, z3::expr const& expression) const {
        if (type->kind == RuntimeType::Kind::boolean) {
            if (expression.is_true()) return {SurfaceValue::Kind::boolean, true};
            if (expression.is_false()) return {SurfaceValue::Kind::boolean, false};
            reject({}, "lift encountered a non-literal Boolean");
        }
        if (type->kind == RuntimeType::Kind::enumeration) {
            for (unsigned i = 0; i < type->enumeration->values.size(); ++i) {
                if (Z3_is_eq_ast(context_, expression,
                                 type->enumeration->values[i]))
                    return {SurfaceValue::Kind::enumeration, false,
                            type->enumeration, i, {}};
            }
            reject({}, "lift encountered a value outside enum `" + type->display + "`");
        }
        if (type->kind == RuntimeType::Kind::tuple && expression.is_app() &&
            expression.num_args() == 2 &&
            Z3_is_eq_func_decl(context_, expression.decl(), *type->tuple_constructor)) {
            SurfaceValue result;
            result.kind = SurfaceValue::Kind::tuple;
            result.elements.push_back(lift_value(type->arguments[0], expression.arg(0)));
            result.elements.push_back(lift_value(type->arguments[1], expression.arg(1)));
            return result;
        }
        reject({}, "lift encountered a value outside admitted type `" + type->display + "`");
    }

    z3::expr reify_value(TypePtr const& type, SurfaceValue const& value) {
        if (type->kind == RuntimeType::Kind::boolean &&
            value.kind == SurfaceValue::Kind::boolean)
            return context_.bool_val(value.boolean);
        if (type->kind == RuntimeType::Kind::enumeration &&
            value.kind == SurfaceValue::Kind::enumeration) {
            if (value.enumeration == type->enumeration &&
                value.case_index < type->enumeration->values.size())
                return type->enumeration->values[value.case_index];
        }
        if (type->kind == RuntimeType::Kind::tuple &&
            value.kind == SurfaceValue::Kind::tuple && value.elements.size() == 2)
            return (*type->tuple_constructor)(
                reify_value(type->arguments[0], value.elements[0]),
                reify_value(type->arguments[1], value.elements[1]));
        throw std::runtime_error("internal Fine surface value/type mismatch");
    }

    SurfaceTable lift_table(TypePtr const& type, z3::expr const& expression) const {
        SurfaceTable result;
        lift_table_into(type, expression, result);
        return result;
    }

    void lift_table_into(TypePtr const& type, z3::expr const& expression,
                         SurfaceTable& output) const {
        if (!expression.is_app()) reject({}, "lift expected an array application");
        if (!Z3_is_eq_sort(context_, expression.get_sort(), type->sort))
            reject({}, "lift received an array with the wrong admitted Table type");
        if (expression.decl().decl_kind() == Z3_OP_CONST_ARRAY) {
            output.default_value = lift_value(type->arguments[1], expression.arg(0));
            return;
        }
        if (expression.decl().decl_kind() == Z3_OP_STORE) {
            lift_table_into(type, expression.arg(0), output);
            output.entries.push_back({
                lift_value(type->arguments[0], expression.arg(1)),
                lift_value(type->arguments[1], expression.arg(2))});
            return;
        }
        reject({}, "array value is outside Fine's admitted table syntax");
    }

    z3::expr reify_table(TypePtr const& type, SurfaceTable const& table) {
        z3::expr result = z3::const_array(
            type->arguments[0]->sort,
            reify_value(type->arguments[1], table.default_value));
        for (SurfaceEntry const& entry : table.entries)
            result = z3::store(result,
                reify_value(type->arguments[0], entry.key),
                reify_value(type->arguments[1], entry.value));
        return result;
    }

    static void print_value(std::ostream& output, SurfaceValue const& value) {
        switch (value.kind) {
        case SurfaceValue::Kind::boolean:
            output << (value.boolean ? "true" : "false");
            return;
        case SurfaceValue::Kind::enumeration:
            output << value.enumeration->case_names[value.case_index];
            return;
        case SurfaceValue::Kind::tuple:
            output << '(';
            for (std::size_t i = 0; i < value.elements.size(); ++i) {
                if (i) output << ", ";
                print_value(output, value.elements[i]);
            }
            output << ')';
            return;
        }
    }

    static void print_table_expression(std::ostream& output,
                                       SurfaceTable const& table) {
        output << "table(default: ";
        print_value(output, table.default_value);
        output << ") {\n";
        for (SurfaceEntry const& entry : table.entries) {
            output << "  ";
            print_value(output, entry.key);
            output << ": ";
            print_value(output, entry.value);
            output << ",\n";
        }
        output << '}';
    }

    static std::string render_model_witness(std::string const& name,
                                            TypePtr const& type,
                                            SurfaceTable const& table) {
        std::ostringstream output;
        output << "model " << name << ": " << type->display << " = ";
        print_table_expression(output, table);
        output << ";\n";
        return output.str();
    }
};

int Runtime::execute(syntax::Document const& document) {
    syntax::ProofDecl const* proof = nullptr;
    syntax::SynthDecl const* synth = nullptr;
    syntax::CheckDecl const* check = nullptr;
    syntax::ModelDecl const* model_hole = nullptr;
    for (syntax::Declaration const& declaration : document.declarations) {
        if (auto const* item = std::get_if<syntax::EnumDecl>(&declaration)) {
            declare_enum(*item);
        } else if (auto const* item = std::get_if<syntax::LetDecl>(&declaration)) {
            declare_let(*item);
        } else if (auto const* item = std::get_if<syntax::ModelDecl>(&declaration)) {
            declare_model(*item);
            if (!item->value) {
                if (model_hole)
                    reject(item->span,
                           "this proof slice admits exactly one model-shaped hole");
                model_hole = item;
            }
        } else if (auto const* item = std::get_if<syntax::ProofDecl>(&declaration)) {
            if (proof || synth || check)
                reject(item->span,
                       "this source slice admits one executable declaration");
            proof = item;
        } else if (auto const* item = std::get_if<syntax::SynthDecl>(&declaration)) {
            if (proof || synth || check)
                reject(item->span,
                       "this source slice admits one executable declaration");
            synth = item;
        } else if (auto const* item = std::get_if<syntax::CheckDecl>(&declaration)) {
            if (proof || synth || check)
                reject(item->span,
                       "this source slice admits one executable declaration");
            check = item;
        } else if (auto const* item =
                       std::get_if<syntax::CounterexampleDecl>(&declaration)) {
            reject(item->span,
                   "a `counterexample` declaration is a returned witness, not an executable check");
        }
    }
    if (synth) return execute_synthesis(*synth);
    if (check) return execute_check(*check);
    if (!proof)
        reject(document.span, "expected one `proof`, `synth`, or `check` declaration");
    if (!model_hole)
        reject(document.span, "expected one model-shaped hole for the proof result");
    return execute_bisimulation(*proof);
}

} // namespace

SemanticError::SemanticError(syntax::SourceSpan span, std::string message)
    : std::runtime_error(std::move(message)), span_(span) {}

std::string SemanticError::format(std::string_view filename,
                                  std::string_view source) const {
    std::size_t offset = std::min(span_.begin.offset, source.size());
    std::size_t line_begin = offset;
    while (line_begin > 0 && source[line_begin - 1] != '\n') --line_begin;
    std::size_t line_end = offset;
    while (line_end < source.size() && source[line_end] != '\n') ++line_end;
    std::string_view line = source.substr(line_begin, line_end - line_begin);
    std::size_t column = std::max<std::size_t>(1, span_.begin.column);
    std::size_t width = std::max<std::size_t>(1, span_.end.offset - span_.begin.offset);

    std::ostringstream output;
    output << filename << ':' << std::max<std::size_t>(1, span_.begin.line)
           << ':' << column << ": error: " << what() << '\n'
           << line << '\n' << std::string(column - 1, ' ') << '^';
    if (width > 1 && width <= line.size()) output << std::string(width - 1, '~');
    return output.str();
}

int execute(syntax::Document const& document, std::ostream& output,
            std::ostream* rainfall_output) {
    return Runtime(output, rainfall_output).execute(document);
}

} // namespace fine
