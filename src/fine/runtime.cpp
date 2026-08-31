#include "runtime.h"

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
    enum class Kind { boolean, enumeration, tuple, table };

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

struct SurfaceValue {
    enum class Kind { boolean, enumeration, tuple };

    Kind kind = Kind::boolean;
    bool boolean = false;
    std::string name;
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
    explicit Runtime(std::ostream& output)
        : output_(output), bool_type_(std::make_shared<RuntimeType>(
                               RuntimeType::Kind::boolean,
                               context_.bool_sort(), "Bool")) {
        types_.emplace("Bool", bool_type_);
    }

    int execute(syntax::Document const& document) {
        syntax::ProofDecl const* proof = nullptr;
        for (syntax::Declaration const& declaration : document.declarations) {
            if (auto const* item = std::get_if<syntax::EnumDecl>(&declaration)) {
                declare_enum(*item);
            } else if (auto const* item = std::get_if<syntax::LetDecl>(&declaration)) {
                declare_let(*item);
            } else if (auto const* item = std::get_if<syntax::ModelDecl>(&declaration)) {
                declare_model(*item);
            } else if (auto const* item = std::get_if<syntax::ProofDecl>(&declaration)) {
                if (proof)
                    reject(item->span, "this source slice admits exactly one proof");
                proof = item;
            }
        }
        if (!proof) reject(document.span, "expected one `proof bisimulation` declaration");
        return execute_bisimulation(*proof);
    }

private:
    z3::context context_;
    std::ostream& output_;
    TypePtr bool_type_;
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
        }
        reject(expression.span, "unknown Fine value form");
    }

    void declare_let(syntax::LetDecl const& declaration) {
        reserve_value_name(declaration.name, declaration.span);
        TypePtr type = resolve_type(declaration.type);
        if (type->kind != RuntimeType::Kind::table)
            reject(declaration.type.span, "a `let` binding in this slice must be a Table");
        TypePtr const& domain = type->arguments[0];
        TypePtr const& range = type->arguments[1];
        z3::expr result = z3::const_array(
            domain->sort, value(declaration.value.default_value, range));
        std::vector<z3::expr> keys;
        for (syntax::TableEntry const& entry : declaration.value.entries) {
            z3::expr key = value(entry.key, domain);
            for (z3::expr const& previous : keys) {
                if (Z3_is_eq_ast(context_, key, previous))
                    reject(entry.key.span, "duplicate table key");
            }
            keys.push_back(key);
            result = z3::store(result, key, value(entry.value, range));
        }
        bindings_.emplace(declaration.name,
                          Binding{type, result, false, declaration.span});
    }

    void declare_model(syntax::ModelDecl const& declaration) {
        reserve_value_name(declaration.name, declaration.span);
        TypePtr type = resolve_type(declaration.type);
        if (type->kind != RuntimeType::Kind::table)
            reject(declaration.type.span, "a `model` hole must have Table type");
        z3::expr hole = context_.constant(declaration.name.c_str(), type->sort);
        bindings_.emplace(declaration.name,
                          Binding{type, hole, true, declaration.span});
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

        z3::solver solver(context_);
        z3::params parameters(context_);
        parameters.set("mbqi", true);
        solver.set(parameters);
        solver.add(z3::forall(left, right,
            z3::implies(related(left, right),
                z3::select(left_label.value, left) ==
                z3::select(right_label.value, right))));
        solver.add(z3::forall(left, right, left_next,
            z3::implies(related(left, right) && steps_left(left, left_next),
                z3::exists(right_next,
                    steps_right(right, right_next) && related(left_next, right_next)))));
        solver.add(z3::forall(left, right, right_next,
            z3::implies(related(left, right) && steps_right(right, right_next),
                z3::exists(left_next,
                    steps_left(left, left_next) && related(left_next, right_next)))));
        solver.add(z3::select(relation.value, initial));

        z3::check_result result = solver.check();
        if (result != z3::sat) {
            std::string detail = result == z3::unknown
                                     ? "unknown: " + solver.reason_unknown()
                                     : "unsatisfiable";
            reject(proof.span, "bisimulation model hole was " + detail);
        }

        z3::model model = solver.get_model();
        z3::expr canonical = z3::const_array(relation_domain->sort, context_.bool_val(false));
        for (z3::expr const& l : left_type->enumeration->values) {
            for (z3::expr const& r : right_type->enumeration->values) {
                z3::expr key = tuple(relation_domain, l, r);
                z3::expr cell = model.eval(z3::select(relation.value, key), true);
                if (!cell.is_true() && !cell.is_false())
                    reject(proof.span, "model returned a non-Boolean relation cell");
                if (cell.is_true())
                    canonical = z3::store(canonical, key, context_.bool_val(true));
            }
        }

        SurfaceTable lifted = lift_table(relation.type, canonical);
        z3::expr roundtrip = reify_table(relation.type, lifted);
        if (!Z3_is_eq_ast(context_, canonical, roundtrip))
            reject(proof.span, "reify(lift(x)) violated exact AST identity");

        output_ << "sat: z3 filled model-shaped hole "
                << inputs.at("relation")->name << '\n';
        print_table(inputs.at("relation")->name, lifted);
        output_ << "reify(lift(x)): exact ast identity (diagnostic ast_id: "
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
            if (expression.is_true()) return {SurfaceValue::Kind::boolean, true, {}, {}};
            if (expression.is_false()) return {SurfaceValue::Kind::boolean, false, {}, {}};
            reject({}, "lift encountered a non-literal Boolean");
        }
        if (type->kind == RuntimeType::Kind::enumeration) {
            for (unsigned i = 0; i < type->enumeration->values.size(); ++i) {
                if (Z3_is_eq_ast(context_, expression,
                                 type->enumeration->values[i]))
                    return {SurfaceValue::Kind::enumeration, false,
                            type->enumeration->case_names[i], {}};
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
            auto found = cases_.find(value.name);
            if (found != cases_.end() && found->second.first == type->enumeration)
                return found->second.first->values[found->second.second];
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
            output << value.name;
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

    void print_table(std::string const& name, SurfaceTable const& table) {
        output_ << "model " << name << " = table(default: ";
        print_value(output_, table.default_value);
        output_ << ") {\n";
        for (SurfaceEntry const& entry : table.entries) {
            output_ << "  ";
            print_value(output_, entry.key);
            output_ << ": ";
            print_value(output_, entry.value);
            output_ << ",\n";
        }
        output_ << "};\n";
    }
};

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

int execute(syntax::Document const& document, std::ostream& output) {
    return Runtime(output).execute(document);
}

} // namespace fine
