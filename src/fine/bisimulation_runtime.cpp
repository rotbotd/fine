#include "runtime_internal.h"

namespace fine::runtime_detail {

    void Runtime::expect_table(Binding const &binding, syntax::SourceSpan span, std::string const &role) {
        if (binding.type->kind != RuntimeType::Kind::table)
            reject(span, role + " must have Table type");
    }

    void Runtime::expect_bool_range(Binding const &binding, syntax::SourceSpan span, std::string const &role) {
        expect_table(binding, span, role);
        if (binding.type->arguments[1]->kind != RuntimeType::Kind::boolean)
            reject(span, role + " must return Bool");
    }

    std::map<std::string, syntax::Expr const *> Runtime::take_map(syntax::SolveDecl const &solve) {
        std::map<std::string, syntax::Expr const *> result;
        for (syntax::NamedArgument const &argument : solve.takes) {
            if (!result.emplace(argument.name, &argument.value).second)
                reject(argument.span, "duplicate solve input `" + argument.name + "`");
        }
        static std::set<std::string> const expected{"relation",   "left_step",   "right_step",
                                                    "left_label", "right_label", "initial"};
        for (auto const &[name, expression] : result) {
            (void)expression;
            if (!expected.contains(name))
                reject(solve.span, "unexpected solve input `" + name + "`");
        }
        for (std::string const &name : expected) {
            if (!result.contains(name))
                reject(solve.span, "missing solve input `" + name + "`");
        }
        return result;
    }

    int Runtime::execute_bisimulation(syntax::SolveDecl const &solve) {
        if (solve.name != "bisimulation")
            reject(solve.span, "unknown solve form `" + solve.name + "`; this slice admits `solve bisimulation`");
        auto inputs = take_map(solve);
        Binding const &relation = binding(*inputs.at("relation"), "relation");
        Binding const &left_step = binding(*inputs.at("left_step"), "left_step");
        Binding const &right_step = binding(*inputs.at("right_step"), "right_step");
        Binding const &left_label = binding(*inputs.at("left_label"), "left_label");
        Binding const &right_label = binding(*inputs.at("right_label"), "right_label");

        expect_bool_range(relation, solve.span, "relation");
        if (!relation.is_model)
            reject(inputs.at("relation")->span, "relation must name the `model` hole");
        TypePtr relation_domain = relation.type->arguments[0];
        if (relation_domain->kind != RuntimeType::Kind::tuple)
            reject(inputs.at("relation")->span, "relation must be indexed by a pair of enum states");
        TypePtr left_type = relation_domain->arguments[0];
        TypePtr right_type = relation_domain->arguments[1];
        if (left_type->kind != RuntimeType::Kind::enumeration || right_type->kind != RuntimeType::Kind::enumeration)
            reject(inputs.at("relation")->span, "bisimulation state types must be finite enums");

        validate_step(left_step, left_type, inputs.at("left_step")->span, "left_step");
        validate_step(right_step, right_type, inputs.at("right_step")->span, "right_step");
        validate_label(left_label, left_type, inputs.at("left_label")->span, "left_label");
        validate_label(right_label, right_type, inputs.at("right_label")->span, "right_label");

        if (solve.gives.kind != syntax::Expr::Kind::name || solve.gives.name != inputs.at("relation")->name)
            reject(solve.gives.span, "`gives` must return the relation model hole");
        z3::expr initial = value(*inputs.at("initial"), relation_domain);

        std::string run_scope = "bisim:" + inputs.at("relation")->name;
        if (rainfall_) {
            rainfall_->record(
                "scope", "bisim.run.open", {run_scope}, "fine.bisimulation",
                "Fine's finite bisimulation elaboration, one public solver query, accepted MBQI bindings, the "
                "public post-preprocessing clause stream, model extensionalization, and source round trip; "
                "excludes every solver-internal boundary not named by a producer event",
                {RainfallRecorder::string_field("relation", rainfall_->term(relation.value)),
                 RainfallRecorder::string_field("left_step", rainfall_->term(left_step.value)),
                 RainfallRecorder::string_field("right_step", rainfall_->term(right_step.value)),
                 RainfallRecorder::string_field("left_label", rainfall_->term(left_label.value)),
                 RainfallRecorder::string_field("right_label", rainfall_->term(right_label.value)),
                 RainfallRecorder::string_field("initial", rainfall_->term(initial)),
                 RainfallRecorder::boolean_field("mbqi", true)});
        }

        z3::expr left = context_.constant("Fine.left", left_type->sort);
        z3::expr right = context_.constant("Fine.right", right_type->sort);
        z3::expr left_next = context_.constant("Fine.left_next", left_type->sort);
        z3::expr right_next = context_.constant("Fine.right_next", right_type->sort);

        auto tuple = [](TypePtr const &type, z3::expr const &first, z3::expr const &second) {
            return (*type->tuple_constructor)(first, second);
        };
        auto related = [&](z3::expr const &l, z3::expr const &r) {
            return z3::select(relation.value, tuple(relation_domain, l, r));
        };
        TypePtr left_step_domain = left_step.type->arguments[0];
        TypePtr right_step_domain = right_step.type->arguments[0];
        auto steps_left = [&](z3::expr const &from, z3::expr const &to) {
            return z3::select(left_step.value, tuple(left_step_domain, from, to));
        };
        auto steps_right = [&](z3::expr const &from, z3::expr const &to) {
            return z3::select(right_step.value, tuple(right_step_domain, from, to));
        };

        auto named_forall = [&](char const *role, std::vector<z3::expr> const &variables, z3::expr const &body) {
            std::vector<Z3_app> bound;
            bound.reserve(variables.size());
            for (z3::expr const &variable : variables)
                bound.push_back(reinterpret_cast<Z3_app>(static_cast<Z3_ast>(variable)));
            std::string qid = std::string("fine.bisim.") + role;
            Z3_ast result = Z3_mk_quantifier_const_ex(context_, true, 0, context_.str_symbol(qid.c_str()),
                                                      context_.str_symbol(""), static_cast<unsigned>(bound.size()),
                                                      bound.data(), 0, nullptr, 0, nullptr, body);
            context_.check_error();
            return z3::expr(context_, result);
        };

        std::vector<std::pair<std::string, z3::expr>> assertions;
        assertions.emplace_back(
            "labels-agree", named_forall("labels-agree", {left, right},
                                         z3::implies(related(left, right), z3::select(left_label.value, left) ==
                                                                               z3::select(right_label.value, right))));
        assertions.emplace_back("left-step-matched",
                                named_forall("left-step-matched", {left, right, left_next},
                                             z3::implies(related(left, right) && steps_left(left, left_next),
                                                         z3::exists(right_next, steps_right(right, right_next) &&
                                                                                    related(left_next, right_next)))));
        assertions.emplace_back("right-step-matched",
                                named_forall("right-step-matched", {left, right, right_next},
                                             z3::implies(related(left, right) && steps_right(right, right_next),
                                                         z3::exists(left_next, steps_left(left, left_next) &&
                                                                                   related(left_next, right_next)))));
        assertions.emplace_back("initial-related", z3::select(relation.value, initial));

        z3::solver solver(context_);
        z3::params parameters(context_);
        parameters.set("mbqi", true);
        parameters.set("ematching", false);
        solver.set(parameters);
        std::vector<std::string> assertion_references;
        for (auto const &[role, assertion] : assertions) {
            solver.add(assertion);
            if (rainfall_) {
                std::string reference = rainfall_->term(assertion);
                assertion_references.push_back(reference);
                rainfall_->source_term(solve.node_id, solve.span, "decl.solve", assertion, "generated", {run_scope});
                rainfall_->record("constraint", "bisim.clause.assert", {run_scope}, "fine.bisimulation",
                                  "Fully elaborated bisimulation clause asserted through Z3's public solver API",
                                  {RainfallRecorder::string_field("role", role),
                                   RainfallRecorder::string_field("assertion", reference)});
            }
        }

        std::string query = "query:0";
        if (rainfall_) {
            rainfall_->record(
                "scope", "solver.query.open", {run_scope, query}, "fine.bisimulation",
                "Public solver assertion boundary with scoped read-only MBQI-binding and on-clause observers",
                {RainfallRecorder::string_field("id", query),
                 RainfallRecorder::string_field("purpose", "find a finite bisimulation relation model"),
                 RainfallRecorder::raw_field("assertions", RainfallRecorder::string_array(assertion_references)),
                 RainfallRecorder::string_field("polarity", "model-exists"),
                 RainfallRecorder::boolean_field("mbqi", true), RainfallRecorder::boolean_field("ematching", false)});
        }

        std::unique_ptr<RainfallQuantifierObserver> quantifier_observer;
        std::unique_ptr<RainfallClauseObserver> clause_observer;
        if (rainfall_) {
            quantifier_observer = std::make_unique<RainfallQuantifierObserver>(
                solver, *rainfall_, std::vector<std::string>{run_scope, query}, false, true);
            clause_observer = std::make_unique<RainfallClauseObserver>(solver, *rainfall_,
                                                                       std::vector<std::string>{run_scope, query});
        }
        z3::check_result result = solver.check();
        quantifier_observer.reset();
        clause_observer.reset();
        if (rainfall_) {
            char const *status = result == z3::sat ? "sat" : result == z3::unsat ? "unsat" : "unknown";
            rainfall_->record(
                "transition", "solver.query.result", {run_scope, query}, "z3.public-api",
                "Final public check result only; no claim about solver search, MBQI steps, or internal cause",
                {RainfallRecorder::string_field("query", query), RainfallRecorder::string_field("status", status),
                 RainfallRecorder::string_field("polarity", "model-exists")});
            rainfall_->record("scope", "solver.query.close", {run_scope, query}, "fine.bisimulation",
                              "Public solver query lifetime", {RainfallRecorder::string_field("id", query)});
        }
        if (result != z3::sat) {
            std::string detail = result == z3::unknown ? "unknown: " + solver.reason_unknown() : "unsatisfiable";
            reject(solve.span, "bisimulation model hole was " + detail);
        }

        z3::model model = solver.get_model();
        z3::expr canonical = z3::const_array(relation_domain->sort, context_.bool_val(false));
        std::string cell_evidence = "[";
        bool first_cell = true;
        for (z3::expr const &l : left_type->enumeration->values) {
            for (z3::expr const &r : right_type->enumeration->values) {
                z3::expr key = tuple(relation_domain, l, r);
                z3::expr selection = z3::select(relation.value, key);
                z3::expr cell = model.eval(selection, true);
                if (!cell.is_true() && !cell.is_false())
                    reject(solve.span, "model returned a non-Boolean relation cell");
                if (rainfall_) {
                    std::string key_reference = rainfall_->term(key);
                    std::string selection_reference = rainfall_->term(selection);
                    std::string value_reference = rainfall_->term(cell);
                    rainfall_->record(
                        "derive", "model.eval-cell", {run_scope}, "z3.public-api",
                        "Completed evaluation of one finite relation selection under the model returned by the "
                        "named satisfiable query",
                        {RainfallRecorder::string_field("evidence_query", query),
                         RainfallRecorder::string_field("key", key_reference),
                         RainfallRecorder::string_field("selection", selection_reference),
                         RainfallRecorder::string_field("value", value_reference),
                         RainfallRecorder::boolean_field("model_completion", true),
                         RainfallRecorder::string_field("relation", "equality-under-this-model")});
                    if (!first_cell)
                        cell_evidence += ',';
                    first_cell = false;
                    cell_evidence += "{\"key\":" + RainfallRecorder::quote(key_reference) +
                                     ",\"selection\":" + RainfallRecorder::quote(selection_reference) +
                                     ",\"value\":" + RainfallRecorder::quote(value_reference) + "}";
                }
                if (cell.is_true())
                    canonical = z3::store(canonical, key, context_.bool_val(true));
            }
        }
        cell_evidence += ']';
        if (rainfall_) {
            rainfall_->record(
                "derive", "bisim.extensionalize-model", {run_scope}, "fine.bisimulation",
                "Complete finite-domain enumeration assembled into Fine's deterministic false-default array "
                "plus true stores",
                {RainfallRecorder::string_field("evidence_query", query),
                 RainfallRecorder::raw_field("cells", cell_evidence),
                 RainfallRecorder::string_field("output", rainfall_->term(canonical)),
                 RainfallRecorder::string_field("policy", "false-default-then-enumeration-order-true-stores")});
        }

        SurfaceTable lifted = lift_table(relation.type, canonical);
        std::string witness_source = render_model_witness(inputs.at("relation")->name, relation.type, lifted);
        syntax::Document witness_document = syntax::parse(witness_source);
        if (witness_document.declarations.size() != 1)
            throw std::runtime_error("internal Fine witness parser returned extra declarations");
        auto const *witness = std::get_if<syntax::ModelDecl>(&witness_document.declarations.front());
        if (!witness || !witness->value || witness->name != inputs.at("relation")->name)
            throw std::runtime_error("internal Fine witness parser changed the declaration");
        TypePtr parsed_type = resolve_type(witness->type);
        if (!same(parsed_type, relation.type))
            throw std::runtime_error("internal Fine witness parser changed the model type");
        z3::expr roundtrip = table_value(parsed_type, *witness->value);
        if (!Z3_is_eq_ast(context_, canonical, roundtrip))
            reject(solve.span, "parse(print(lift(x))) violated exact AST identity after reification");

        if (rainfall_) {
            rainfall_->record(
                "object", "fine.model-witness", {run_scope}, "fine.runtime",
                "Lifted, printed, parsed, and elaborated model witness with exact same-manager AST identity",
                {RainfallRecorder::string_field("declaration", inputs.at("relation")->name),
                 RainfallRecorder::string_field("source", witness_source),
                 RainfallRecorder::string_field("semantic_term", rainfall_->term(canonical)),
                 RainfallRecorder::boolean_field("parse_reify_exact_identity", true)});
            rainfall_->record("transition", "fine.witness.accept", {run_scope}, "fine.runtime",
                              "Satisfiable model query plus finite extensionalization and Fine source "
                              "round-trip identity check",
                              {RainfallRecorder::string_field("declaration", inputs.at("relation")->name),
                               RainfallRecorder::string_field("evidence_query", query),
                               RainfallRecorder::string_field("status", "model-witness"),
                               RainfallRecorder::boolean_field("source_roundtrip_exact_identity", true)});
            rainfall_->validate_terms();
            rainfall_->record("scope", "bisim.run.close", {run_scope}, "fine.runtime",
                              "Finite bisimulation model and Fine source witness round trip",
                              {RainfallRecorder::string_field("status", "model-witness")});
        }

        output_ << "sat: z3 filled model-shaped hole " << inputs.at("relation")->name << '\n';
        output_ << witness_source;
        output_ << "parse(print(lift(x))): exact ast identity (diagnostic ast_id: "
                << Z3_get_ast_id(context_, canonical) << ")\n";
        return 0;
    }

    void Runtime::validate_step(Binding const &step, TypePtr const &state, syntax::SourceSpan span,
                                std::string const &role) {
        expect_bool_range(step, span, role);
        TypePtr domain = step.type->arguments[0];
        if (domain->kind != RuntimeType::Kind::tuple || !same(domain->arguments[0], state) ||
            !same(domain->arguments[1], state))
            reject(span, role + " must have type Table((" + state->display + ", " + state->display + "), Bool)");
    }

    void Runtime::validate_label(Binding const &label, TypePtr const &state, syntax::SourceSpan span,
                                 std::string const &role) {
        expect_bool_range(label, span, role);
        if (!same(label.type->arguments[0], state))
            reject(span, role + " must have type Table(" + state->display + ", Bool)");
    }

    SurfaceValue Runtime::lift_value(TypePtr const &type, z3::expr const &expression) const {
        if (type->kind == RuntimeType::Kind::boolean) {
            if (expression.is_true())
                return {SurfaceValue::Kind::boolean, true};
            if (expression.is_false())
                return {SurfaceValue::Kind::boolean, false};
            reject({}, "lift encountered a non-literal Boolean");
        }
        if (type->kind == RuntimeType::Kind::enumeration) {
            for (unsigned i = 0; i < type->enumeration->values.size(); ++i) {
                if (Z3_is_eq_ast(context_, expression, type->enumeration->values[i]))
                    return {SurfaceValue::Kind::enumeration, false, type->enumeration, i, {}};
            }
            reject({}, "lift encountered a value outside enum `" + type->display + "`");
        }
        if (type->kind == RuntimeType::Kind::datatype && expression.is_app()) {
            for (unsigned i = 0; i < type->datatype->cases.size(); ++i) {
                DatatypeCaseInfo const &item = type->datatype->cases[i];
                if (!Z3_is_eq_func_decl(context_, expression.decl(), item.constructor))
                    continue;
                SurfaceValue result;
                result.kind = SurfaceValue::Kind::datatype;
                result.case_index = i;
                result.datatype = type->datatype;
                for (unsigned j = 0; j < expression.num_args(); ++j)
                    result.elements.push_back(lift_value(item.field_types[j], expression.arg(j)));
                return result;
            }
            reject({}, "lift encountered a value outside datatype `" + type->display + "`");
        }
        if (type->kind == RuntimeType::Kind::tuple && expression.is_app() && expression.num_args() == 2 &&
            Z3_is_eq_func_decl(context_, expression.decl(), *type->tuple_constructor)) {
            SurfaceValue result;
            result.kind = SurfaceValue::Kind::tuple;
            result.elements.push_back(lift_value(type->arguments[0], expression.arg(0)));
            result.elements.push_back(lift_value(type->arguments[1], expression.arg(1)));
            return result;
        }
        reject({}, "lift encountered a value outside admitted type `" + type->display + "`");
    }

    z3::expr Runtime::reify_value(TypePtr const &type, SurfaceValue const &value) {
        if (type->kind == RuntimeType::Kind::boolean && value.kind == SurfaceValue::Kind::boolean)
            return context_.bool_val(value.boolean);
        if (type->kind == RuntimeType::Kind::enumeration && value.kind == SurfaceValue::Kind::enumeration) {
            if (value.enumeration == type->enumeration && value.case_index < type->enumeration->values.size())
                return type->enumeration->values[value.case_index];
        }
        if (type->kind == RuntimeType::Kind::datatype && value.kind == SurfaceValue::Kind::datatype &&
            value.datatype == type->datatype && value.case_index < type->datatype->cases.size()) {
            DatatypeCaseInfo const &item = type->datatype->cases[value.case_index];
            if (value.elements.size() != item.field_types.size())
                throw std::runtime_error("internal Fine datatype surface arity mismatch");
            std::vector<z3::expr> arguments;
            arguments.reserve(value.elements.size());
            for (std::size_t i = 0; i < value.elements.size(); ++i)
                arguments.push_back(reify_value(item.field_types[i], value.elements[i]));
            return item.constructor(static_cast<unsigned>(arguments.size()), arguments.data());
        }
        if (type->kind == RuntimeType::Kind::tuple && value.kind == SurfaceValue::Kind::tuple &&
            value.elements.size() == 2)
            return (*type->tuple_constructor)(reify_value(type->arguments[0], value.elements[0]),
                                              reify_value(type->arguments[1], value.elements[1]));
        throw std::runtime_error("internal Fine surface value/type mismatch");
    }

    SurfaceTable Runtime::lift_table(TypePtr const &type, z3::expr const &expression) const {
        SurfaceTable result;
        lift_table_into(type, expression, result);
        return result;
    }

    void Runtime::lift_table_into(TypePtr const &type, z3::expr const &expression, SurfaceTable &output) const {
        if (!expression.is_app())
            reject({}, "lift expected an array application");
        if (!Z3_is_eq_sort(context_, expression.get_sort(), type->sort))
            reject({}, "lift received an array with the wrong admitted Table type");
        if (expression.decl().decl_kind() == Z3_OP_CONST_ARRAY) {
            output.default_value = lift_value(type->arguments[1], expression.arg(0));
            return;
        }
        if (expression.decl().decl_kind() == Z3_OP_STORE) {
            lift_table_into(type, expression.arg(0), output);
            output.entries.push_back(
                {lift_value(type->arguments[0], expression.arg(1)), lift_value(type->arguments[1], expression.arg(2))});
            return;
        }
        reject({}, "array value is outside Fine's admitted table syntax");
    }

    z3::expr Runtime::reify_table(TypePtr const &type, SurfaceTable const &table) {
        z3::expr result =
            z3::const_array(type->arguments[0]->sort, reify_value(type->arguments[1], table.default_value));
        for (SurfaceEntry const &entry : table.entries)
            result = z3::store(result, reify_value(type->arguments[0], entry.key),
                               reify_value(type->arguments[1], entry.value));
        return result;
    }

    void Runtime::print_value(std::ostream &output, SurfaceValue const &value) {
        switch (value.kind) {
        case SurfaceValue::Kind::boolean: output << (value.boolean ? "true" : "false"); return;
        case SurfaceValue::Kind::enumeration: output << value.enumeration->case_names[value.case_index]; return;
        case SurfaceValue::Kind::datatype: {
            DatatypeCaseInfo const &item = value.datatype->cases[value.case_index];
            output << item.name;
            if (!value.elements.empty()) {
                output << '(';
                for (std::size_t i = 0; i < value.elements.size(); ++i) {
                    if (i)
                        output << ", ";
                    print_value(output, value.elements[i]);
                }
                output << ')';
            }
            return;
        }
        case SurfaceValue::Kind::tuple:
            output << '(';
            for (std::size_t i = 0; i < value.elements.size(); ++i) {
                if (i)
                    output << ", ";
                print_value(output, value.elements[i]);
            }
            output << ')';
            return;
        }
    }

    void Runtime::print_table_expression(std::ostream &output, SurfaceTable const &table) {
        output << "table(default: ";
        print_value(output, table.default_value);
        output << ") {\n";
        for (SurfaceEntry const &entry : table.entries) {
            output << "  ";
            print_value(output, entry.key);
            output << ": ";
            print_value(output, entry.value);
            output << ",\n";
        }
        output << '}';
    }

    std::string Runtime::render_model_witness(std::string const &name, TypePtr const &type, SurfaceTable const &table) {
        std::ostringstream output;
        output << "model " << name << ": " << type->display << " = ";
        print_table_expression(output, table);
        output << ";\n";
        return output.str();
    }

}  // namespace fine::runtime_detail
