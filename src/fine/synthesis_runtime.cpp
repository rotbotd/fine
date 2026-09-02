#include "runtime_internal.h"

namespace fine::runtime_detail {

    int Runtime::execute_match_synthesis(syntax::SynthDecl const &declaration) {
        reserve_value_name(declaration.name, declaration.span);
        TypePtr result_type = resolve_type(declaration.result_type);
        if (!same(result_type, int_type_))
            reject(declaration.result_type.span, "the first match-synthesis slice returns Int");
        if (!declaration.scrutinee || declaration.scrutinee->kind != syntax::Expr::Kind::name)
            reject(declaration.span, "a synthesis match must inspect one named parameter directly");

        struct ParameterInfo {
            syntax::Parameter const *source;
            TypePtr type;
            z3::expr term;
        };
        ExpressionEnvironment environment;
        std::vector<ParameterInfo> parameters;
        std::size_t matched_index = 0;
        bool found_match = false;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const &parameter = declaration.parameters[i];
            if (parameter.name == "result")
                reject(parameter.span, "`result` is reserved for the synthesis result");
            TypePtr type = resolve_type(parameter.type);
            if (type->kind == RuntimeType::Kind::table)
                reject(parameter.type.span, "synthesis parameters cannot have Table type");
            z3::expr term =
                context_.constant(("Fine.synth." + declaration.name + ".arg" + std::to_string(i)).c_str(), type->sort);
            if (!environment.emplace(parameter.name, TypedExpression{type, term}).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            parameters.push_back({&parameter, type, term});
            if (parameter.name == declaration.scrutinee->name) {
                matched_index = i;
                found_match = true;
            }
        }
        if (!found_match)
            reject(declaration.scrutinee->span, "the matched name is not a synthesis parameter");
        TypePtr matched_type = parameters[matched_index].type;
        z3::expr matched_term = parameters[matched_index].term;
        if (matched_type->kind != RuntimeType::Kind::datatype)
            reject(declaration.scrutinee->span, "a synthesis match requires a field-bearing datatype parameter");

        z3::expr result = context_.int_const(("Fine.synth." + declaration.name + ".result").c_str());
        ExpressionEnvironment specification_environment = environment;
        specification_environment.emplace("result", TypedExpression{int_type_, result});
        z3::expr specification = context_.bool_val(true);
        for (syntax::Expr const &condition : declaration.ensures) {
            TypedExpression elaborated = elaborate_expression(condition, specification_environment);
            if (!same(elaborated.type, bool_type_))
                reject(condition.span, "an ensured condition must have type Bool");
            specification = specification && elaborated.expression;
        }

        DatatypeInfo *datatype = matched_type->datatype;
        std::map<std::string, std::size_t> case_indices;
        for (std::size_t i = 0; i < datatype->cases.size(); ++i)
            case_indices.emplace(datatype->cases[i].name, i);
        std::set<std::string> seen_cases;
        std::set<std::string> seen_holes;
        std::vector<std::pair<z3::expr, z3::expr>> branches;
        std::vector<syntax::Expr> materialized;
        std::size_t open_arms = 0;
        std::size_t selections = 0;
        std::size_t core_members = 0;
        std::string replacements_json = "[";

        if (rainfall_) {
            rainfall_->record("scope", "synth.run.open", {"synth:" + declaration.name}, "fine.synthesis",
                              "Fine-owned exhaustive match skeleton with independently typed open arms",
                              {RainfallRecorder::string_field("name", declaration.name),
                               RainfallRecorder::string_field("matched_parameter", declaration.scrutinee->name),
                               RainfallRecorder::string_field("specification", rainfall_->term(specification)),
                               RainfallRecorder::number_field("arms", declaration.arms.size())});
        }

        for (syntax::MatchArm const &arm : declaration.arms) {
            auto found_case = case_indices.find(arm.constructor);
            if (found_case == case_indices.end())
                reject(arm.span, "constructor `" + arm.constructor + "` does not belong to `" + datatype->name + "`");
            if (!seen_cases.insert(arm.constructor).second)
                reject(arm.span, "duplicate match arm for `" + arm.constructor + "`");
            DatatypeCaseInfo const &item = datatype->cases[found_case->second];
            if (arm.bindings.size() != item.field_types.size())
                reject(arm.span, "constructor `" + arm.constructor + "` expects " +
                                     std::to_string(item.field_types.size()) + " pattern bindings");

            std::vector<z3::expr> field_symbols;
            std::vector<z3::expr> field_accessors;
            std::vector<z3::expr> arm_parameters;
            std::vector<z3::expr> grammar_inputs;
            std::vector<std::pair<std::string, z3::expr>> named_inputs;
            ExpressionEnvironment arm_environment = environment;
            std::set<std::string> local_bindings;
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                if (i == matched_index)
                    continue;
                arm_parameters.push_back(parameters[i].term);
                if (same(parameters[i].type, int_type_)) {
                    grammar_inputs.push_back(parameters[i].term);
                    named_inputs.emplace_back(parameters[i].source->name, parameters[i].term);
                }
            }
            for (std::size_t i = 0; i < arm.bindings.size(); ++i) {
                syntax::MatchBinding const &binding = arm.bindings[i];
                z3::expr symbol = context_.constant(
                    ("Fine.synth." + declaration.name + ".arm." + arm.constructor + ".field" + std::to_string(i))
                        .c_str(),
                    item.field_types[i]->sort);
                field_symbols.push_back(symbol);
                field_accessors.push_back(item.accessors[i](matched_term));
                arm_parameters.push_back(symbol);
                if (binding.name == "_")
                    continue;
                if (!local_bindings.insert(binding.name).second || arm_environment.contains(binding.name))
                    reject(binding.span, "duplicate or shadowing pattern binding `" + binding.name + "`");
                arm_environment.emplace(binding.name, TypedExpression{item.field_types[i], symbol});
                if (same(item.field_types[i], int_type_)) {
                    grammar_inputs.push_back(symbol);
                    named_inputs.emplace_back(binding.name, symbol);
                }
            }
            z3::expr constructed = item.constructor(static_cast<unsigned>(field_symbols.size()), field_symbols.data());
            arm_environment.insert_or_assign(declaration.scrutinee->name, TypedExpression{matched_type, constructed});
            z3::expr_vector source(context_), destination(context_);
            source.push_back(matched_term);
            destination.push_back(constructed);
            z3::expr arm_specification = specification.substitute(source, destination);

            z3::expr arm_value(context_);
            syntax::Expr surface;
            if (arm.value.kind == syntax::Expr::Kind::hole) {
                if (!seen_holes.insert(arm.value.name).second)
                    reject(arm.value.span, "duplicate synthesis hole `?" + arm.value.name + "`");
                ++open_arms;
                std::string arm_name = declaration.name + "." + arm.value.name;
                if (rainfall_) {
                    std::string source_id = rainfall_->source_node(arm.value.node_id, arm.value.span, "expr.hole");
                    std::vector<std::string> grammar_refs;
                    for (z3::expr const &input : grammar_inputs)
                        grammar_refs.push_back(rainfall_->term(input));
                    rainfall_->record(
                        "object", "synth.hole.declare", {"synth:" + declaration.name}, "fine.synthesis",
                        "Snapshot-scoped typed source hole and its fixed integer grammar inputs",
                        {RainfallRecorder::string_field("id", "hole:" + std::to_string(arm.value.node_id)),
                         RainfallRecorder::string_field("snapshot", "snapshot:0"),
                         RainfallRecorder::string_field("source", source_id),
                         RainfallRecorder::string_field("name", arm.value.name),
                         RainfallRecorder::string_field("expected_type", "Int"),
                         RainfallRecorder::string_field("grammar", "fine.qf-lia-int.v1"),
                         RainfallRecorder::raw_field("grammar_inputs", RainfallRecorder::string_array(grammar_refs))});
                }
                RefutationSynthesizer synthesizer(context_, arm_name, arm_parameters, result, arm_specification,
                                                  rainfall_.get(), grammar_inputs, true);
                SynthesisResult synthesized = synthesizer.run();
                selections += synthesized.selections.size();
                core_members += synthesized.core_indices.size();
                syntax::Expr lifted = lift_expression(synthesized.witness, named_inputs);
                std::ostringstream rendered;
                print_expression(rendered, lifted);
                syntax::Expr reparsed = syntax::parse_expression(rendered.str());
                TypedExpression roundtrip = elaborate_expression(reparsed, arm_environment);
                if (!same(roundtrip.type, result_type) ||
                    !Z3_is_eq_ast(context_, roundtrip.expression, synthesized.witness))
                    reject(arm.value.span, "open-arm witness failed exact Fine source round trip");
                surface = std::move(lifted);
                arm_value = synthesized.witness;
                if (rainfall_) {
                    rainfall_->record(
                        "transition", "synth.arm.close", {"synth-arm:" + arm_name}, "fine.synthesis",
                        "One open match arm produced an independently verified witness",
                        {RainfallRecorder::string_field("hole", "hole:" + std::to_string(arm.value.node_id)),
                         RainfallRecorder::string_field("constructor", arm.constructor),
                         RainfallRecorder::string_field("body", rendered.str()),
                         RainfallRecorder::string_field("semantic_term", rainfall_->term(arm_value)),
                         RainfallRecorder::string_field("status", "verified")});
                }
                if (replacements_json.size() > 1)
                    replacements_json += ',';
                replacements_json +=
                    "{\"hole\":" + RainfallRecorder::quote("hole:" + std::to_string(arm.value.node_id)) +
                    ",\"from\":" + std::to_string(arm.value.span.begin.offset) +
                    ",\"to\":" + std::to_string(arm.value.span.end.offset) +
                    ",\"insert\":" + RainfallRecorder::quote(rendered.str()) + '}';
            }
            else {
                std::function<void(syntax::Expr const &)> reject_nested_hole = [&](syntax::Expr const &expression) {
                    if (expression.kind == syntax::Expr::Kind::hole)
                        reject(expression.span, "a synthesis hole must occupy an entire match arm");
                    for (syntax::Expr const &child : expression.elements)
                        reject_nested_hole(child);
                };
                reject_nested_hole(arm.value);
                TypedExpression elaborated = elaborate_expression(arm.value, arm_environment);
                if (!same(elaborated.type, result_type))
                    reject(arm.value.span, "match arm must return `" + result_type->display + "`");
                arm_value = elaborated.expression;
                surface = arm.value;
            }

            if (!field_symbols.empty()) {
                z3::expr_vector fields(context_), accessors(context_);
                for (z3::expr const &field : field_symbols)
                    fields.push_back(field);
                for (z3::expr const &accessor : field_accessors)
                    accessors.push_back(accessor);
                arm_value = arm_value.substitute(fields, accessors);
            }
            branches.emplace_back(item.recognizer(matched_term), arm_value);
            materialized.push_back(std::move(surface));
        }
        replacements_json += ']';

        if (seen_cases.size() != datatype->cases.size()) {
            std::string missing;
            for (DatatypeCaseInfo const &item : datatype->cases) {
                if (!seen_cases.contains(item.name)) {
                    if (!missing.empty())
                        missing += ", ";
                    missing += "`" + item.name + "`";
                }
            }
            reject(declaration.span, "non-exhaustive synthesis match; missing " + missing);
        }
        z3::expr body = branches.back().second;
        for (std::size_t i = branches.size() - 1; i-- > 0;)
            body = z3::ite(branches[i].first, branches[i].second, body);
        z3::expr_vector result_source(context_), result_destination(context_);
        result_source.push_back(result);
        result_destination.push_back(body);
        z3::expr verified_specification = specification.substitute(result_source, result_destination);
        z3::expr counterexample_query = !verified_specification;
        std::string const run_scope = "synth:" + declaration.name;
        std::string const verification_query = "query:match-verify";
        if (rainfall_) {
            rainfall_->record("constraint", "synth.match.counterexample.assert", {run_scope}, "fine.synthesis",
                              "Negation of the completed exhaustive match specification",
                              {RainfallRecorder::string_field("body", rainfall_->term(body)),
                               RainfallRecorder::string_field("specification", rainfall_->term(verified_specification)),
                               RainfallRecorder::string_field("assertion", rainfall_->term(counterexample_query))});
            rainfall_->record("scope", "solver.query.open", {run_scope, verification_query}, "fine.synthesis",
                              "Fresh public solver query for the complete materialized match",
                              {RainfallRecorder::string_field("id", verification_query),
                               RainfallRecorder::string_field("purpose", "refute the completed exhaustive match"),
                               RainfallRecorder::string_field("assertion", rainfall_->term(counterexample_query)),
                               RainfallRecorder::string_field("polarity", "counterexample-exists"),
                               RainfallRecorder::boolean_field("mbqi", true),
                               RainfallRecorder::boolean_field("ematching", true)});
        }
        z3::solver verifier(context_);
        verifier.add(counterexample_query);
        z3::check_result verification = verifier.check();
        if (rainfall_) {
            char const *status = verification == z3::sat ? "sat" : verification == z3::unsat ? "unsat" : "unknown";
            rainfall_->record(
                "transition", "solver.query.result", {run_scope, verification_query}, "z3.public-api",
                "Final public result for the whole-match verification query",
                {RainfallRecorder::string_field("query", verification_query),
                 RainfallRecorder::string_field("status", status),
                 RainfallRecorder::string_field("polarity", "counterexample-exists"),
                 RainfallRecorder::string_field("domain_outcome", verification == z3::sat     ? "refuted"
                                                                  : verification == z3::unsat ? "verified"
                                                                                              : "unknown")});
            rainfall_->record("scope", "solver.query.close", {run_scope, verification_query}, "fine.synthesis",
                              "Whole-match public solver query lifetime",
                              {RainfallRecorder::string_field("id", verification_query)});
        }
        if (verification == z3::unknown)
            reject(declaration.span, "materialized match verification was unknown: " + verifier.reason_unknown());
        if (verification != z3::unsat)
            reject(declaration.span, "materialized match does not satisfy its specification");

        if (rainfall_) {
            rainfall_->source_term(declaration.node_id, declaration.span, "decl.synth", body, "generated",
                                   {"synth:" + declaration.name});
            rainfall_->record("object", "fine.match-witness", {"synth:" + declaration.name}, "fine.runtime",
                              "Whole verified match plus exact source-span replacements for its open arms",
                              {RainfallRecorder::string_field("declaration", declaration.name),
                               RainfallRecorder::string_field("semantic_term", rainfall_->term(body)),
                               RainfallRecorder::raw_field("replacements", replacements_json),
                               RainfallRecorder::number_field("open_arms", open_arms),
                               RainfallRecorder::boolean_field("verified", true)});
            rainfall_->validate_terms();
            rainfall_->record(
                "scope", "synth.run.close", {"synth:" + declaration.name}, "fine.runtime",
                "Exhaustive match with every open arm synthesized and the whole body freshly verified",
                {RainfallRecorder::string_field("status", open_arms ? "source-program" : "verified-materialized"),
                 RainfallRecorder::number_field("open_arms", open_arms)});
        }

        output_ << (open_arms ? "source-match: synthesized " : "verified-match: ") << declaration.name << " with "
                << open_arms << " open arms; selected " << selections << " ground instances; cores kept "
                << core_members << '\n';
        output_ << "match " << declaration.scrutinee->name << " {\n";
        for (std::size_t i = 0; i < declaration.arms.size(); ++i) {
            syntax::MatchArm const &arm = declaration.arms[i];
            output_ << "  " << arm.constructor;
            if (!arm.bindings.empty()) {
                output_ << '(';
                for (std::size_t j = 0; j < arm.bindings.size(); ++j) {
                    if (j)
                        output_ << ", ";
                    output_ << arm.bindings[j].name;
                }
                output_ << ')';
            }
            output_ << " => ";
            print_expression(output_, materialized[i]);
            output_ << ",\n";
        }
        output_ << "}\nverification: no counterexample\n";
        return 0;
    }

    int Runtime::execute_synthesis(syntax::SynthDecl const &declaration) {
        if (declaration.scrutinee)
            return execute_match_synthesis(declaration);
        reserve_value_name(declaration.name, declaration.span);
        TypePtr result_type = resolve_type(declaration.result_type);
        if (!same(result_type, int_type_))
            reject(declaration.result_type.span, "the first synthesis slice returns Int");
        if (declaration.parameters.empty())
            reject(declaration.span, "a synthesized function needs a parameter");

        ExpressionEnvironment parameter_environment;
        std::vector<std::pair<std::string, z3::expr>> named_parameters;
        std::vector<z3::expr> parameters;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const &parameter = declaration.parameters[i];
            if (parameter.name == "result")
                reject(parameter.span, "`result` is reserved for the synthesis result");
            TypePtr type = resolve_type(parameter.type);
            if (!same(type, int_type_))
                reject(parameter.type.span, "the first synthesis slice admits only Int parameters");
            std::string internal = "Fine.synth." + declaration.name + ".arg" + std::to_string(i);
            z3::expr value = context_.int_const(internal.c_str());
            if (!parameter_environment.emplace(parameter.name, TypedExpression{type, value}).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            named_parameters.emplace_back(parameter.name, value);
            parameters.push_back(value);
        }

        std::string result_name = "Fine.synth." + declaration.name + ".result";
        z3::expr result = context_.int_const(result_name.c_str());
        ExpressionEnvironment specification_environment = parameter_environment;
        specification_environment.emplace("result", TypedExpression{int_type_, result});
        z3::expr specification = context_.bool_val(true);
        for (syntax::Expr const &condition : declaration.ensures) {
            TypedExpression elaborated = elaborate_expression(condition, specification_environment);
            if (!same(elaborated.type, bool_type_))
                reject(condition.span, "an ensured condition must have type Bool");
            specification = specification && elaborated.expression;
        }

        RefutationSynthesizer synthesizer(context_, declaration.name, parameters, result, specification,
                                          rainfall_.get());
        SynthesisResult synthesized = synthesizer.run();
        syntax::Expr lifted = lift_expression(synthesized.witness, named_parameters);
        std::ostringstream rendered;
        print_expression(rendered, lifted);
        std::string body = rendered.str();
        syntax::Expr reparsed = syntax::parse_expression(body);
        TypedExpression roundtrip = elaborate_expression(reparsed, parameter_environment);
        if (!same(roundtrip.type, result_type) || !Z3_is_eq_ast(context_, roundtrip.expression, synthesized.witness))
            reject(declaration.span, "parse(print(lift(witness))) violated exact AST identity");

        if (rainfall_) {
            rainfall_->record("object", "fine.source-witness", {"synth:" + declaration.name}, "fine.runtime",
                              "Lifted, printed, parsed, elaborated witness with exact same-manager AST identity",
                              {RainfallRecorder::string_field("declaration", declaration.name),
                               RainfallRecorder::string_field("body", body),
                               RainfallRecorder::string_field("semantic_term", rainfall_->term(synthesized.witness)),
                               RainfallRecorder::boolean_field("parse_reify_exact_identity", true)});
            rainfall_->record("transition", "fine.witness.accept", {"synth:" + declaration.name}, "fine.runtime",
                              "Backend verification plus Fine source round-trip identity check",
                              {RainfallRecorder::string_field("declaration", declaration.name),
                               RainfallRecorder::string_field("status", "source-program"),
                               RainfallRecorder::boolean_field("verified", true)});
            rainfall_->validate_terms();
            rainfall_->record("scope", "synth.run.close", {"synth:" + declaration.name}, "fine.runtime",
                              "Native synthesis plus Fine source witness round trip",
                              {RainfallRecorder::string_field("status", "source-program")});
        }

        output_ << "source-program: synthesized " << declaration.name << " from " << synthesized.selections.size()
                << " ground instances; core kept " << synthesized.core_indices.size() << '\n';
        output_ << body << '\n';
        output_ << "verification: no counterexample\n";
        output_ << "parse(print(lift(witness))): exact ast identity (diagnostic ast_id: "
                << Z3_get_ast_id(context_, synthesized.witness) << ")\n";
        return 0;
    }

}  // namespace fine::runtime_detail
