#include "elaboration_internal.h"

// Runtime-value semantics: native enum datatypes, value expressions, checked
// value functions, and calls. Proof access is limited to ProofContext.
namespace fine::elaboration {

    ValueElaborator::ValueElaborator(std::ostream &output, ExecutionOptions const &options,
                                     MaterializationSink &materializations)
        : output_(output), options_(options), materializations_(materializations) {}

    std::vector<std::string> ValueElaborator::runtime_kind_names() const {
        std::vector<std::string> names;
        for (auto const &[name, enumeration] : enums_)
            names.push_back(name);
        return names;
    }

    z3::sort ValueElaborator::sort(ValueKind const &kind) {
        if (kind.tag == ValueKind::Tag::integer)
            return context_.int_sort();
        if (kind.tag == ValueKind::Tag::boolean)
            return context_.bool_sort();
        auto found = enums_.find(kind.name);
        if (found == enums_.end())
            throw std::logic_error("unknown runtime enum kind `" + kind.name + "`");
        return found->second->sort;
    }
    void ValueElaborator::require_known_type(syntax::ValueType const &type) {
        if (type.kind == syntax::ValueType::Kind::enumeration && !enums_.contains(type.name))
            reject(type.span, "unknown value type `" + type.name + "`");
    }
    void ValueElaborator::declare_enum(syntax::EnumDecl const &declaration) {
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
                field_names.push_back(context_.str_symbol(
                    ("fine.enum." + declaration.name + "." + constructor.name + ".field" + std::to_string(field_index))
                        .c_str()));
                field_sorts.push_back(field_kind == kind ? context_.datatype_sort(sort_symbol) : sort(field_kind));
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
            z3_constructors.query(static_cast<unsigned>(i), constructor.constructor, constructor.tester, accessors);
            for (unsigned j = 0; j < accessors.size(); ++j)
                constructor.accessors.push_back(accessors[j]);
            runtime->constructors.push_back(std::move(constructor));
        }
        RuntimeEnum *stored = runtime.get();
        enums_.emplace(declaration.name, std::move(runtime));
        for (std::size_t i = 0; i < stored->constructors.size(); ++i)
            constructors_.emplace(stored->constructors[i].name, std::pair{stored, i});
        output_ << "declared enum: " << declaration.name << " (" << stored->constructors.size() << " constructors)\n";
    }
    void ValueElaborator::record_boundary() {
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
    ValueTerm ValueElaborator::elaborate_constructor(syntax::ValueExpr const &expression, RuntimeEnum &enumeration,
                                                     RuntimeConstructor const &constructor,
                                                     ValueEnvironment const &values, ProofEnvironment const &proofs,
                                                     std::vector<std::string> const &proof_order,
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
                reject(expression.elements[i].span,
                       "field " + std::to_string(i) + " of `" + constructor.name + "` has the wrong value type");
            arguments.push_back(field.expression);
        }
        return {enumeration.kind, constructor.constructor(arguments)};
    }
    ValueTerm ValueElaborator::elaborate_match(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                               ProofEnvironment const &proofs,
                                               std::vector<std::string> const &proof_order,
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
                reject(expression.match_arm_spans[i],
                       "match arm `" + name + "` expects " + std::to_string(constructor.fields.size()) + " binders");
            ValueEnvironment branch_values = values;
            std::set<std::string> arm_names;
            for (std::size_t j = 0; j < binders.size(); ++j) {
                if (!arm_names.insert(binders[j]).second)
                    reject(expression.match_arm_spans[i], "duplicate pattern binder `" + binders[j] + "`");
                branch_values.insert_or_assign(
                    binders[j], ValueTerm(constructor.fields[j], constructor.accessors[j](scrutinee.expression)));
            }
            ValueTerm body = elaborate_value(expression.elements[i + 1], branch_values, proofs, proof_order, absorbed);
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
    ValueTerm ValueElaborator::elaborate_value(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                               ProofEnvironment const &proofs,
                                               std::vector<std::string> const &proof_order,
                                               std::vector<z3::expr> const &absorbed) {
        switch (expression.kind) {
        case syntax::ValueExpr::Kind::name: {
            auto value = values.find(expression.name);
            if (value != values.end())
                return value->second;
            if (proofs.contains(expression.name))
                reject(expression.span, "proof `" + expression.name + "` cannot inhabit a runtime value");
            if (proofs_->has_constructor(expression.name))
                reject(expression.span, "proof constructor `" + expression.name + "` cannot inhabit a runtime value");
            if (auto found = constructors_.find(expression.name); found != constructors_.end()) {
                RuntimeConstructor const &constructor = found->second.first->constructors[found->second.second];
                if (!constructor.fields.empty())
                    reject(expression.span, "enum constructor `" + expression.name + "` expects " +
                                                std::to_string(constructor.fields.size()) + " fields");
                syntax::ValueExpr nullary = expression;
                nullary.kind = syntax::ValueExpr::Kind::call;
                return elaborate_constructor(nullary, *found->second.first, constructor, values, proofs, proof_order,
                                             absorbed);
            }
            reject(expression.span, "unknown value `" + expression.name + "`");
        }
        case syntax::ValueExpr::Kind::integer:
            return {integer_kind(), context_.int_val(expression.integer_text.c_str())};
        case syntax::ValueExpr::Kind::boolean: return {boolean_kind(), context_.bool_val(expression.boolean_value)};
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
                                             found->second.first->constructors[found->second.second], values, proofs,
                                             proof_order, absorbed);
            return elaborate_call(expression, values, proofs, proof_order, absorbed);
        }
        case syntax::ValueExpr::Kind::match: return elaborate_match(expression, values, proofs, proof_order, absorbed);
        }
        reject(expression.span, "unsupported value expression");
    }

    bool ValueElaborator::contains_parameter(syntax::ValueExpr const &expression,
                                             std::map<std::string, ValueKind> const &parameters,
                                             ValueEnvironment const &bindings) {
        if (expression.kind == syntax::ValueExpr::Kind::name && parameters.contains(expression.name) &&
            !bindings.contains(expression.name))
            return true;
        return std::any_of(
            expression.elements.begin(), expression.elements.end(),
            [&](syntax::ValueExpr const &element) { return contains_parameter(element, parameters, bindings); });
    }
    bool ValueElaborator::match_constructor_index(syntax::ValueExpr const &pattern, z3::expr const &target,
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

        auto match_runtime_constructor = [&](std::string const &name, std::vector<syntax::ValueExpr> const &arguments) {
            auto found = constructors_.find(name);
            if (found == constructors_.end() || !target.is_app())
                return false;
            RuntimeConstructor const &constructor = found->second.first->constructors[found->second.second];
            if (arguments.size() != constructor.fields.size() || target.num_args() != arguments.size() ||
                !Z3_is_eq_func_decl(context_, target.decl(), constructor.constructor))
                return false;
            for (std::size_t i = 0; i < arguments.size(); ++i)
                if (!match_constructor_index(arguments[i], target.arg(static_cast<unsigned>(i)), parameters, bindings))
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

    void ValueElaborator::declare_function(syntax::FunctionDecl const &declaration) {
        if (functions_.contains(declaration.name) || proofs_->has_function(declaration.name) ||
            constructors_.contains(declaration.name))
            reject(declaration.span, "duplicate function `" + declaration.name + "`");
        ValueEnvironment values;
        ProofEnvironment proofs;
        std::vector<std::string> proof_order;
        std::vector<z3::expr> absorbed;
        std::set<std::string> names;
        for (auto const &parameter : declaration.parameters) {
            if (parameter.name == "result")
                reject(parameter.span, "`result` is reserved for the function body inside `ensures`");
            if (!names.insert(parameter.name).second)
                reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
            require_known_type(parameter.type);
            ValueKind kind = kind_of(parameter.type);
            std::string symbol = "fine." + declaration.name + "." + parameter.name;
            values.emplace(parameter.name, ValueTerm(kind, context_.constant(symbol.c_str(), sort(kind))));
        }
        for (auto const &coeffect : declaration.coeffects) {
            if (coeffect.name == "result")
                reject(coeffect.span, "`result` is reserved for the function body inside `ensures`");
            if (!names.insert(coeffect.name).second)
                reject(coeffect.span, "duplicate parameter `" + coeffect.name + "`");
            IdentityType type = proofs_->elaborate_identity(coeffect.type, values, proofs, proof_order, absorbed);
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
            proofs_->absorb(found->second, absorbed, {"function:" + declaration.name}, "hypothetical-coeffect",
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
            z3::expr counterexample_query = !guarantee;
            solver.add(counterexample_query);
            z3::check_result status = solver.check();
            if (status == z3::unknown) {
                if (rainfall_) {
                    rainfall_->validate_terms();
                    rainfall_->record(
                        "scope", "function.guarantee.unknown.close", {"function:" + declaration.name},
                        "fine.value-elaborator",
                        "The counterexample query returned unknown; Fine does not manufacture a witness",
                        {RainfallRecorder::string_field("function", declaration.name),
                         RainfallRecorder::string_field("status", "unknown"),
                         RainfallRecorder::string_field("reason", solver.reason_unknown())});
                }
                reject(declaration.span, "function `" + declaration.name + "` guarantee check was unknown: " +
                                             solver.reason_unknown());
            }
            if (status == z3::sat)
                reject_with_counterexample(declaration, values, absorbed, body, guarantee, solver.get_model());
        }
        functions_.emplace(declaration.name, &declaration);
        ++functions_verified_;
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
    ValueTerm ValueElaborator::elaborate_call(syntax::ValueExpr const &expression,
                                              ValueEnvironment const &caller_values,
                                              ProofEnvironment const &caller_proofs,
                                              std::vector<std::string> const &caller_proof_order,
                                              std::vector<z3::expr> const &caller_absorbed) {
        auto found = functions_.find(expression.name);
        if (found == functions_.end()) {
            if (proofs_->has_function(expression.name))
                reject(expression.span,
                       "proof function `" + expression.name + "` cannot be called from a runtime value expression");
            if (proofs_->has_constructor(expression.name))
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
            IdentityType demand = proofs_->elaborate_identity(coeffect.type, callee_values, callee_proofs,
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
            proofs_->absorb(inserted->second, callee_absorbed, {"call:" + expression.name}, "resolved-coeffect");
            chosen.emplace_back(coeffect.name, proof_name);
            ++coeffects_resolved_;
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
            materializations_.request_materialization(syntax::ConcreteRange::empty_at(expression.call_argument_end),
                                                      insertion.str(), expression.span);
        }
        ValueTerm result =
            elaborate_value(function.body, callee_values, callee_proofs, callee_proof_order, callee_absorbed);
        require_known_type(function.result_type);
        if (result.kind != kind_of(function.result_type))
            reject(expression.span, "internal function result type mismatch");
        return result;
    }

}  // namespace fine::elaboration
