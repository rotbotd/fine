#include "elaboration_internal.h"

// Typed counterexample decoding is a separate value-language consumer. It owns
// model completion and source roundtripping; ordinary value elaboration only
// decides that the negated guarantee is satisfiable and delegates here.
namespace fine::elaboration {

    syntax::ValueExpr ValueElaborator::lift_model_value(z3::expr const &expression, ValueKind const &kind) const {
        syntax::ValueExpr result;
        if (kind.tag == ValueKind::Tag::boolean) {
            if (!expression.is_true() && !expression.is_false())
                throw std::logic_error("completed Bool model value is not a literal");
            result.kind = syntax::ValueExpr::Kind::boolean;
            result.boolean_value = expression.is_true();
            return result;
        }
        if (kind.tag == ValueKind::Tag::integer) {
            std::string numeral;
            if (!expression.is_numeral(numeral))
                throw std::logic_error("completed Int model value is not a numeral");
            result.kind = syntax::ValueExpr::Kind::integer;
            result.integer_text = std::move(numeral);
            return result;
        }

        auto found = enums_.find(kind.name);
        if (found == enums_.end() || !expression.is_app())
            throw std::logic_error("completed model value is outside runtime enum `" + kind.name + "`");
        RuntimeEnum const &enumeration = *found->second;
        for (RuntimeConstructor const &constructor : enumeration.constructors) {
            if (!Z3_is_eq_func_decl(context_, expression.decl(), constructor.constructor))
                continue;
            if (expression.num_args() != constructor.fields.size())
                throw std::logic_error("completed enum model value has the wrong constructor arity");
            result.kind = constructor.fields.empty() ? syntax::ValueExpr::Kind::name : syntax::ValueExpr::Kind::call;
            result.name = constructor.name;
            for (unsigned i = 0; i < expression.num_args(); ++i)
                result.elements.push_back(lift_model_value(expression.arg(i), constructor.fields[i]));
            return result;
        }
        throw std::logic_error("completed model value is outside runtime enum `" + kind.name + "`");
    }

    [[noreturn]] void ValueElaborator::reject_with_counterexample(
        syntax::FunctionDecl const &declaration, ValueEnvironment const &values,
        std::vector<z3::expr> const &absorbed, ValueTerm const &body, z3::expr const &guarantee,
        z3::model const &model) {
        struct Assignment {
            std::string name;
            ValueKind kind;
            z3::expr value;
            std::string source;
        };
        std::vector<Assignment> assignments;
        assignments.reserve(declaration.parameters.size() + 1);
        for (auto const &parameter : declaration.parameters) {
            ValueTerm const &term = values.at(parameter.name);
            z3::expr value = model.eval(term.expression, true);
            assignments.push_back({parameter.name, term.kind, value,
                                   print_value(lift_model_value(value, term.kind))});
        }
        z3::expr result_value = model.eval(body.expression, true);
        assignments.push_back(
            {"result", body.kind, result_value, print_value(lift_model_value(result_value, body.kind))});

        std::ostringstream rendered;
        rendered << "counterexample " << declaration.name;
        if (!declaration.coeffects.empty()) {
            rendered << " takes [";
            for (std::size_t i = 0; i < declaration.coeffects.size(); ++i) {
                if (i)
                    rendered << ", ";
                rendered << declaration.coeffects[i].name;
            }
            rendered << ']';
        }
        rendered << " {\n";
        for (auto const &assignment : assignments)
            rendered << "  " << assignment.name << ": " << kind_name(assignment.kind) << " = "
                     << assignment.source << ";\n";
        rendered << "}\n";
        std::string witness_source = rendered.str();

        syntax::CounterexampleWitness witness = syntax::parse_counterexample_witness(witness_source);
        if (witness.function != declaration.name || witness.entries.size() != assignments.size())
            throw std::logic_error("counterexample source parser changed its witness shape");
        if (witness.assumed_coeffects.size() != declaration.coeffects.size())
            throw std::logic_error("counterexample source parser changed its coeffect domain");
        for (std::size_t i = 0; i < declaration.coeffects.size(); ++i)
            if (witness.assumed_coeffects[i] != declaration.coeffects[i].name)
                throw std::logic_error("counterexample source parser changed a coeffect name");

        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        for (std::size_t i = 0; i < assignments.size(); ++i) {
            auto const &assignment = assignments[i];
            auto const &entry = witness.entries[i];
            require_known_type(entry.type);
            if (entry.name != assignment.name || kind_of(entry.type) != assignment.kind)
                throw std::logic_error("counterexample source parser changed an assignment type or name");
            ValueEnvironment no_values;
            ValueTerm roundtrip = elaborate_value(entry.value, no_values, no_proofs, no_proof_order, no_absorbed);
            if (roundtrip.kind != assignment.kind || !same_ast(context_, roundtrip.expression, assignment.value))
                throw std::logic_error("parse(print(lift(model value))) violated exact AST identity");
        }

        z3::solver witness_check(context_);
        for (auto const &assumption : absorbed)
            witness_check.add(assumption);
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i)
            witness_check.add(values.at(declaration.parameters[i].name).expression == assignments[i].value);
        witness_check.add(guarantee);
        if (witness_check.check() != z3::unsat)
            throw std::logic_error("lifted counterexample assignments do not refute the function guarantee");

        output_ << witness_source;
        output_ << "parse(print(lift(values))): exact ast identity\n";
        if (rainfall_) {
            std::vector<std::string> names;
            names.reserve(assignments.size());
            for (std::size_t i = 0; i < assignments.size(); ++i) {
                auto const &assignment = assignments[i];
                names.push_back(assignment.name);
                z3::expr original = i < declaration.parameters.size()
                                        ? values.at(declaration.parameters[i].name).expression
                                        : body.expression;
                rainfall_->record(
                    "derive", "model.eval-assignment", {"function:" + declaration.name}, "z3.public-api",
                    "One typed function input or result was completed under the counterexample model",
                    {RainfallRecorder::string_field("function", declaration.name),
                     RainfallRecorder::string_field("assignment", assignment.name),
                     RainfallRecorder::string_field("type", kind_name(assignment.kind)),
                     RainfallRecorder::string_field("term", rainfall_->term(original, "counterexample-term")),
                     RainfallRecorder::string_field("value", rainfall_->term(assignment.value, "counterexample-value")),
                     RainfallRecorder::string_field("source", assignment.source),
                     RainfallRecorder::boolean_field("model_completion", true)});
            }
            rainfall_->record(
                "object", "fine.counterexample.witness", {"function:" + declaration.name},
                "fine.value-counterexample",
                "Every completed model value was lifted, printed, parsed, and reified to exact AST identity",
                {RainfallRecorder::string_field("function", declaration.name),
                 RainfallRecorder::string_field("source", witness_source),
                 RainfallRecorder::raw_field("assignments", RainfallRecorder::string_array(names)),
                 RainfallRecorder::raw_field("assumed_coeffects",
                                             RainfallRecorder::string_array(witness.assumed_coeffects)),
                 RainfallRecorder::boolean_field("parse_reify_exact_identity", true)});
            rainfall_->record(
                "transition", "fine.counterexample.verify", {"function:" + declaration.name},
                "fine.value-counterexample",
                "The parsed assignments plus declared coeffects make the original guarantee impossible",
                {RainfallRecorder::string_field("function", declaration.name),
                 RainfallRecorder::string_field("status", "unsat"),
                 RainfallRecorder::boolean_field("original_guarantee_rechecked", true)});
            rainfall_->validate_terms();
            rainfall_->record(
                "scope", "function.counterexample.close", {"function:" + declaration.name},
                "fine.value-counterexample",
                "A satisfiable negated guarantee produced a checked source-level counterexample",
                {RainfallRecorder::string_field("function", declaration.name),
                 RainfallRecorder::string_field("status", "counterexample-witness")});
        }
        reject(declaration.span, "function `" + declaration.name +
                                     "` does not satisfy its guarantees under declared coeffects; "
                                     "counterexample emitted");
    }

}  // namespace fine::elaboration
