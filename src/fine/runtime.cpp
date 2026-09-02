#include "runtime_internal.h"

namespace fine {
    namespace runtime_detail {

        [[noreturn]] void reject(syntax::SourceSpan span, std::string message) {
            throw SemanticError(span, std::move(message));
        }

        Runtime::Runtime(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                         std::string rainfall_run)
            : output_(output), fixedpoint_(context_),
              rainfall_(rainfall_output ? std::make_unique<RainfallRecorder>(context_, *rainfall_output,
                                                                             std::move(rainfall_run), snapshot)
                                        : nullptr),
              bool_type_(std::make_shared<RuntimeType>(RuntimeType::Kind::boolean, context_.bool_sort(), "Bool")),
              int_type_(std::make_shared<RuntimeType>(RuntimeType::Kind::integer, context_.int_sort(), "Int")) {
            types_.emplace("Bool", bool_type_);
            types_.emplace("Int", int_type_);
        }

        char const *Runtime::expression_syntax_kind(syntax::Expr const &expression) {
            switch (expression.kind) {
            case syntax::Expr::Kind::name: return "expr.name";
            case syntax::Expr::Kind::boolean: return "expr.boolean";
            case syntax::Expr::Kind::integer: return "expr.integer";
            case syntax::Expr::Kind::tuple: return "expr.tuple";
            case syntax::Expr::Kind::call: return "expr.call";
            case syntax::Expr::Kind::binary: return "expr.binary";
            case syntax::Expr::Kind::conditional: return "expr.conditional";
            case syntax::Expr::Kind::hole: return "expr.hole";
            }
            return "expr.unknown";
        }

        char const *Runtime::expression_correspondence(syntax::Expr const &expression) {
            switch (expression.kind) {
            case syntax::Expr::Kind::name:
            case syntax::Expr::Kind::boolean:
            case syntax::Expr::Kind::integer: return "exact";
            case syntax::Expr::Kind::tuple:
            case syntax::Expr::Kind::call:
            case syntax::Expr::Kind::binary:
            case syntax::Expr::Kind::conditional: return "desugared";
            case syntax::Expr::Kind::hole: return "generated";
            }
            return "desugared";
        }

        TypedExpression Runtime::completed_expression(syntax::Expr const &source, TypePtr type, z3::expr expression) {
            if (rainfall_ && capture_source_edges_)
                rainfall_->source_term(source.node_id, source.span, expression_syntax_kind(source), expression,
                                       expression_correspondence(source), source_edge_within_);
            return {std::move(type), std::move(expression)};
        }

        void Runtime::declare_expression_sources(syntax::Expr const &expression) {
            rainfall_->source_node(expression.node_id, expression.span, expression_syntax_kind(expression));
            for (syntax::Expr const &child : expression.elements)
                declare_expression_sources(child);
        }

        void Runtime::declare_table_sources(syntax::TableLiteral const &table) {
            declare_expression_sources(table.default_value);
            for (syntax::TableEntry const &entry : table.entries) {
                declare_expression_sources(entry.key);
                declare_expression_sources(entry.value);
            }
        }

        void Runtime::declare_document_sources(syntax::Document const &document) {
            if (!rainfall_)
                return;
            for (syntax::Declaration const &declaration : document.declarations) {
                std::visit(
                    [&](auto const &item) {
                        using T = std::decay_t<decltype(item)>;
                        char const *kind = "decl.unknown";
                        if constexpr (std::is_same_v<T, syntax::EnumDecl>)
                            kind = "decl.enum";
                        else if constexpr (std::is_same_v<T, syntax::LetDecl>)
                            kind = "decl.let";
                        else if constexpr (std::is_same_v<T, syntax::ModelDecl>)
                            kind = "decl.model";
                        else if constexpr (std::is_same_v<T, syntax::ProofDecl>)
                            kind = "decl.proof";
                        else if constexpr (std::is_same_v<T, syntax::ViewDecl>)
                            kind = "decl.view";
                        else if constexpr (std::is_same_v<T, syntax::ProofFamilyDecl>)
                            kind = "decl.proof-family";
                        else if constexpr (std::is_same_v<T, syntax::FunctionDecl>)
                            kind = "decl.function";
                        else if constexpr (std::is_same_v<T, syntax::SynthDecl>)
                            kind = "decl.synth";
                        else if constexpr (std::is_same_v<T, syntax::CheckDecl>)
                            kind = "decl.check";
                        else if constexpr (std::is_same_v<T, syntax::CounterexampleDecl>)
                            kind = "decl.counterexample";
                        rainfall_->source_node(item.node_id, item.span, kind);

                        if constexpr (std::is_same_v<T, syntax::LetDecl>) {
                            declare_table_sources(item.value);
                        }
                        else if constexpr (std::is_same_v<T, syntax::ModelDecl>) {
                            if (item.value)
                                declare_table_sources(*item.value);
                        }
                        else if constexpr (std::is_same_v<T, syntax::ProofDecl>) {
                            for (syntax::NamedArgument const &argument : item.takes)
                                declare_expression_sources(argument.value);
                            declare_expression_sources(item.gives);
                        }
                        else if constexpr (std::is_same_v<T, syntax::ViewDecl>) {
                            for (syntax::Expr const &requirement : item.requirements)
                                declare_expression_sources(requirement);
                            if (item.witness)
                                declare_expression_sources(*item.witness);
                        }
                        else if constexpr (std::is_same_v<T, syntax::ProofFamilyDecl>) {
                            for (syntax::ProofConstructor const &constructor : item.constructors) {
                                for (syntax::Expr const &premise : constructor.premises)
                                    declare_expression_sources(premise);
                                for (syntax::ArbitraryPremise const &field : constructor.arbitrary_premises) {
                                    rainfall_->source_node(field.node_id, field.span, "proof.arbitrary-field");
                                    for (syntax::Expr const &argument : field.view_arguments)
                                        declare_expression_sources(argument);
                                    for (syntax::Expr const &premise : field.premises)
                                        declare_expression_sources(premise);
                                }
                                declare_expression_sources(constructor.result);
                            }
                        }
                        else if constexpr (std::is_same_v<T, syntax::FunctionDecl>) {
                            declare_expression_sources(item.scrutinee);
                            for (syntax::MatchArm const &arm : item.arms)
                                declare_expression_sources(arm.value);
                        }
                        else if constexpr (std::is_same_v<T, syntax::SynthDecl>) {
                            if (item.scrutinee)
                                declare_expression_sources(*item.scrutinee);
                            for (syntax::MatchArm const &arm : item.arms)
                                declare_expression_sources(arm.value);
                            for (syntax::Expr const &condition : item.ensures)
                                declare_expression_sources(condition);
                        }
                        else if constexpr (std::is_same_v<T, syntax::CheckDecl>) {
                            if (item.proof_induction)
                                declare_expression_sources(*item.proof_induction);
                            for (syntax::Expr const &condition : item.assumes)
                                declare_expression_sources(condition);
                            for (syntax::Expr const &condition : item.ensures)
                                declare_expression_sources(condition);
                        }
                        else if constexpr (std::is_same_v<T, syntax::CounterexampleDecl>) {
                            for (syntax::CounterexampleEntry const &entry : item.entries)
                                declare_expression_sources(entry.value);
                        }
                    },
                    declaration);
            }
        }

        void Runtime::reserve_type_name(std::string const &name, syntax::SourceSpan span) {
            if (types_.contains(name))
                reject(span, "type `" + name + "` is already declared");
        }

        void Runtime::reserve_value_name(std::string const &name, syntax::SourceSpan span) {
            if (!value_names_.insert(name).second)
                reject(span, "value `" + name + "` is already declared");
        }

        void Runtime::declare_enum(syntax::EnumDecl const &declaration) {
            reserve_type_name(declaration.name, declaration.span);
            std::set<std::string> local_cases;
            for (std::size_t i = 0; i < declaration.cases.size(); ++i) {
                std::string const &name = declaration.cases[i].name;
                if (!local_cases.insert(name).second)
                    reject(declaration.cases[i].span, "duplicate enum case `" + name + "`");
                reserve_value_name(name, declaration.cases[i].span);
                std::set<std::string> local_fields;
                for (syntax::EnumField const &field : declaration.cases[i].fields) {
                    if (!local_fields.insert(field.name).second)
                        reject(field.span, "duplicate field `" + field.name + "` in constructor `" + name + "`");
                }
            }

            bool has_fields = std::any_of(declaration.cases.begin(), declaration.cases.end(),
                                          [](syntax::EnumCase const &item) { return !item.fields.empty(); });
            if (has_fields) {
                declare_datatype(declaration);
                return;
            }

            std::vector<char const *> names;
            names.reserve(declaration.cases.size());
            for (syntax::EnumCase const &item : declaration.cases)
                names.push_back(item.name.c_str());
            z3::func_decl_vector constructors(context_);
            z3::func_decl_vector testers(context_);
            z3::sort sort = context_.enumeration_sort(declaration.name.c_str(), static_cast<unsigned>(names.size()),
                                                      names.data(), constructors, testers);

            auto info = std::make_unique<EnumInfo>();
            info->name = declaration.name;
            for (syntax::EnumCase const &item : declaration.cases)
                info->case_names.push_back(item.name);
            TypePtr type = std::make_shared<RuntimeType>(RuntimeType::Kind::enumeration, sort, declaration.name);
            info->type = type;
            type->enumeration = info.get();
            for (unsigned i = 0; i < constructors.size(); ++i) {
                info->constructors.push_back(constructors[i]);
                info->values.push_back(constructors[i]());
            }
            EnumInfo *stable = info.get();
            for (unsigned i = 0; i < stable->case_names.size(); ++i)
                cases_.emplace(stable->case_names[i], std::make_pair(stable, i));
            types_.emplace(declaration.name, type);
            enums_.emplace(declaration.name, std::move(info));
        }

        void Runtime::declare_datatype(syntax::EnumDecl const &declaration) {
            z3::constructors z3_constructors(context_);
            std::vector<std::vector<bool>> self_fields;
            std::vector<std::vector<TypePtr>> resolved_fields;
            self_fields.reserve(declaration.cases.size());
            resolved_fields.reserve(declaration.cases.size());

            z3::symbol datatype_name = context_.str_symbol(declaration.name.c_str());
            for (syntax::EnumCase const &item : declaration.cases) {
                std::vector<z3::symbol> field_names;
                std::vector<z3::sort> field_sorts;
                std::vector<bool> case_self_fields;
                std::vector<TypePtr> case_resolved_fields;
                for (syntax::EnumField const &field : item.fields) {
                    field_names.push_back(context_.str_symbol(field.name.c_str()));
                    bool self = field.type.kind == syntax::Type::Kind::named && field.type.name == declaration.name;
                    case_self_fields.push_back(self);
                    if (self) {
                        field_sorts.push_back(context_.datatype_sort(datatype_name));
                        case_resolved_fields.push_back(nullptr);
                    }
                    else {
                        TypePtr type = resolve_type(field.type);
                        if (type->kind == RuntimeType::Kind::table)
                            reject(field.type.span, "Table fields are outside Fine's first datatype slice");
                        field_sorts.push_back(type->sort);
                        case_resolved_fields.push_back(type);
                    }
                }
                std::string recognizer = "is_" + item.name;
                z3_constructors.add(context_.str_symbol(item.name.c_str()), context_.str_symbol(recognizer.c_str()),
                                    static_cast<unsigned>(field_names.size()), field_names.data(), field_sorts.data());
                self_fields.push_back(std::move(case_self_fields));
                resolved_fields.push_back(std::move(case_resolved_fields));
            }

            z3::sort sort = context_.datatype(datatype_name, z3_constructors);
            auto info = std::make_unique<DatatypeInfo>();
            info->name = declaration.name;
            TypePtr type = std::make_shared<RuntimeType>(RuntimeType::Kind::datatype, sort, declaration.name);
            info->type = type;
            type->datatype = info.get();

            for (std::size_t i = 0; i < declaration.cases.size(); ++i) {
                syntax::EnumCase const &item = declaration.cases[i];
                DatatypeCaseInfo case_info(context_);
                case_info.name = item.name;
                z3::func_decl_vector accessors(context_);
                z3_constructors.query(static_cast<unsigned>(i), case_info.constructor, case_info.recognizer, accessors);
                for (std::size_t j = 0; j < item.fields.size(); ++j) {
                    case_info.field_names.push_back(item.fields[j].name);
                    case_info.field_types.push_back(self_fields[i][j] ? type : resolved_fields[i][j]);
                    case_info.accessors.push_back(accessors[static_cast<unsigned>(j)]);
                }
                info->cases.push_back(std::move(case_info));
            }

            DatatypeInfo *stable = info.get();
            for (unsigned i = 0; i < stable->cases.size(); ++i)
                constructors_.emplace(stable->cases[i].name, std::make_pair(stable, i));
            types_.emplace(declaration.name, type);
            datatypes_.emplace(declaration.name, std::move(info));
        }

        void Runtime::declare_function(syntax::FunctionDecl const &declaration) {
            reserve_value_name(declaration.name, declaration.span);
            if (declaration.parameters.empty())
                reject(declaration.span, "a function needs at least one parameter");
            if (declaration.scrutinee.kind != syntax::Expr::Kind::name)
                reject(declaration.scrutinee.span, "the first function slice matches one named parameter directly");

            ExpressionEnvironment environment;
            std::vector<TypePtr> parameter_types;
            std::vector<z3::sort> parameter_sorts;
            z3::expr_vector formal_parameters(context_);
            TypePtr matched_type;
            z3::expr matched_term(context_);
            bool found_match = false;
            std::size_t matched_parameter_index = 0;
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                syntax::Parameter const &parameter = declaration.parameters[i];
                TypePtr type = resolve_type(parameter.type);
                if (type->kind == RuntimeType::Kind::table)
                    reject(parameter.type.span, "function parameters cannot have Table type in this slice");
                std::string internal = "Fine.function." + declaration.name + ".arg" + std::to_string(i);
                z3::expr term = context_.constant(internal.c_str(), type->sort);
                if (!environment.emplace(parameter.name, TypedExpression{type, term}).second)
                    reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
                parameter_types.push_back(type);
                parameter_sorts.push_back(type->sort);
                formal_parameters.push_back(term);
                if (parameter.name == declaration.scrutinee.name) {
                    matched_type = type;
                    matched_term = term;
                    found_match = true;
                    matched_parameter_index = i;
                }
            }
            if (!found_match)
                reject(declaration.scrutinee.span, "the matched name is not a function parameter");
            if (matched_type->kind != RuntimeType::Kind::datatype)
                reject(declaration.scrutinee.span, "a function match requires a field-bearing datatype");

            TypePtr result_type = resolve_type(declaration.result_type);
            if (result_type->kind == RuntimeType::Kind::table)
                reject(declaration.result_type.span, "functions cannot return Table in this slice");
            auto info = std::make_unique<FunctionInfo>(context_);
            info->name = declaration.name;
            info->parameter_types = parameter_types;
            info->result_type = result_type;
            info->declaration = context_.recfun(declaration.name.c_str(), static_cast<unsigned>(parameter_sorts.size()),
                                                parameter_sorts.data(), result_type->sort);
            FunctionInfo *stable = info.get();
            functions_.emplace(declaration.name, std::move(info));

            DatatypeInfo *datatype = matched_type->datatype;
            std::map<std::string, std::size_t> case_indices;
            for (std::size_t i = 0; i < datatype->cases.size(); ++i)
                case_indices.emplace(datatype->cases[i].name, i);
            std::set<std::string> seen_cases;
            std::vector<std::pair<z3::expr, z3::expr>> branches;
            branches.reserve(declaration.arms.size());

            bool previous_capture = capture_source_edges_;
            std::vector<std::string> previous_within = source_edge_within_;
            capture_source_edges_ = true;
            source_edge_within_ = {"function:" + declaration.name};
            for (syntax::MatchArm const &arm : declaration.arms) {
                auto found_case = case_indices.find(arm.constructor);
                if (found_case == case_indices.end())
                    reject(arm.span,
                           "constructor `" + arm.constructor + "` does not belong to `" + datatype->name + "`");
                if (!seen_cases.insert(arm.constructor).second)
                    reject(arm.span, "duplicate match arm for `" + arm.constructor + "`");
                DatatypeCaseInfo const &item = datatype->cases[found_case->second];
                if (arm.bindings.size() != item.field_types.size())
                    reject(arm.span, "constructor `" + arm.constructor + "` expects " +
                                         std::to_string(item.field_types.size()) + " pattern bindings");
                ExpressionEnvironment arm_environment = environment;
                std::set<std::string> local_bindings;
                std::set<std::string> recursive_bindings;
                for (std::size_t i = 0; i < arm.bindings.size(); ++i) {
                    syntax::MatchBinding const &binding = arm.bindings[i];
                    if (binding.name == "_")
                        continue;
                    if (!local_bindings.insert(binding.name).second || arm_environment.contains(binding.name))
                        reject(binding.span, "duplicate or shadowing pattern binding `" + binding.name + "`");
                    arm_environment.emplace(binding.name,
                                            TypedExpression{item.field_types[i], item.accessors[i](matched_term)});
                    if (same(item.field_types[i], matched_type))
                        recursive_bindings.insert(binding.name);
                }
                std::function<void(syntax::Expr const &)> check_structural_calls = [&](syntax::Expr const &expression) {
                    if (expression.kind == syntax::Expr::Kind::call && expression.name == declaration.name) {
                        if (expression.elements.size() != declaration.parameters.size())
                            reject(expression.span, "recursive call to `" + declaration.name + "` has the wrong arity");
                        syntax::Expr const &decreasing = expression.elements[matched_parameter_index];
                        if (decreasing.kind != syntax::Expr::Kind::name ||
                            !recursive_bindings.contains(decreasing.name))
                            reject(decreasing.span, "recursive call to `" + declaration.name +
                                                        "` must pass a direct `" + datatype->name +
                                                        "` pattern field as its matched argument");
                    }
                    for (syntax::Expr const &child : expression.elements)
                        check_structural_calls(child);
                };
                check_structural_calls(arm.value);
                TypedExpression value = elaborate_expression(arm.value, arm_environment);
                if (!same(value.type, result_type))
                    reject(arm.value.span, "match arm must return `" + result_type->display + "`");
                branches.emplace_back(item.recognizer(matched_term), value.expression);
            }
            capture_source_edges_ = previous_capture;
            source_edge_within_ = std::move(previous_within);
            if (seen_cases.size() != datatype->cases.size()) {
                std::string missing;
                for (DatatypeCaseInfo const &item : datatype->cases) {
                    if (!seen_cases.contains(item.name)) {
                        if (!missing.empty())
                            missing += ", ";
                        missing += "`" + item.name + "`";
                    }
                }
                reject(declaration.span, "non-exhaustive match; missing " + missing);
            }

            z3::expr body = branches.back().second;
            for (std::size_t i = branches.size() - 1; i-- > 0;)
                body = z3::ite(branches[i].first, branches[i].second, body);
            context_.recdef(stable->declaration, formal_parameters, body);

            if (rainfall_) {
                std::string scope = "function:" + declaration.name;
                z3::expr application = stable->declaration(formal_parameters);
                rainfall_->source_term(declaration.node_id, declaration.span, "decl.function", body, "generated",
                                       {scope});
                rainfall_->record(
                    "transform", "function.recursive-definition", {scope}, "fine.elaborator",
                    "Compiler-owned exhaustive datatype match registered through Z3_add_rec_def; records the "
                    "formal application and body but does not pretend Z3 stores a source-level equation",
                    {RainfallRecorder::string_field("function", declaration.name),
                     RainfallRecorder::string_field("matched_parameter", declaration.scrutinee.name),
                     RainfallRecorder::string_field("formal_application", rainfall_->term(application)),
                     RainfallRecorder::string_field("definition_body", rainfall_->term(body)),
                     RainfallRecorder::number_field("arms", branches.size())});
            }
        }

        TypePtr Runtime::tuple_type(TypePtr const &first, TypePtr const &second, syntax::SourceSpan span) {
            if (first->kind == RuntimeType::Kind::table || second->kind == RuntimeType::Kind::table)
                reject(span, "table values cannot be tuple components in Fine v1");
            std::string key = "(" + first->display + ", " + second->display + ")";
            auto found = compound_types_.find(key);
            if (found != compound_types_.end())
                return found->second;

            char const *fields[] = {"first", "second"};
            z3::sort sorts[] = {first->sort, second->sort};
            z3::func_decl_vector projections(context_);
            std::string z3_name = "Fine.Pair." + std::to_string(tuple_sequence_++);
            z3::func_decl constructor = context_.tuple_sort(z3_name.c_str(), 2, fields, sorts, projections);
            TypePtr result = std::make_shared<RuntimeType>(RuntimeType::Kind::tuple, constructor.range(), key);
            result->arguments = {first, second};
            result->tuple_constructor = std::make_unique<z3::func_decl>(constructor);
            compound_types_.emplace(key, result);
            return result;
        }

        TypePtr Runtime::resolve_type(syntax::Type const &syntax_type) {
            if (syntax_type.kind == syntax::Type::Kind::named) {
                auto found = types_.find(syntax_type.name);
                if (found == types_.end())
                    reject(syntax_type.span, "unknown type `" + syntax_type.name + "`");
                return found->second;
            }

            if (syntax_type.arguments.size() != 2) {
                std::string noun = syntax_type.kind == syntax::Type::Kind::tuple ? "tuple" : "Table";
                reject(syntax_type.span, noun + " requires exactly two type arguments");
            }
            TypePtr first = resolve_type(syntax_type.arguments[0]);
            TypePtr second = resolve_type(syntax_type.arguments[1]);

            if (syntax_type.kind == syntax::Type::Kind::tuple)
                return tuple_type(first, second, syntax_type.span);

            if (first->kind == RuntimeType::Kind::table || second->kind == RuntimeType::Kind::table)
                reject(syntax_type.span, "nested tables are outside Fine v1");
            std::string key = "Table(" + first->display + ", " + second->display + ")";
            auto found = compound_types_.find(key);
            if (found != compound_types_.end())
                return found->second;
            TypePtr result = std::make_shared<RuntimeType>(RuntimeType::Kind::table,
                                                           context_.array_sort(first->sort, second->sort), key);
            result->arguments = {first, second};
            compound_types_.emplace(key, result);
            return result;
        }

        void Runtime::declare_view(syntax::ViewDecl const &declaration) {
            if (views_.contains(declaration.name) || types_.contains(declaration.name) ||
                functions_.contains(declaration.name) || proof_families_.contains(declaration.name))
                reject(declaration.span, "duplicate type-level name `" + declaration.name + "`");

            ViewInfo info;
            info.name = declaration.name;
            info.carrier = resolve_type(declaration.carrier);
            if (info.carrier->kind == RuntimeType::Kind::table)
                reject(declaration.carrier.span, "a constrained view must keep one existing native Fine carrier");
            info.requirements = declaration.requirements;
            info.witness = declaration.witness;

            ExpressionEnvironment environment;
            std::set<std::string> names;
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                syntax::Parameter const &parameter = declaration.parameters[i];
                if (parameter.name == "value")
                    reject(parameter.span, "`value` is reserved for the constrained-view carrier");
                if (!names.insert(parameter.name).second)
                    reject(parameter.span, "duplicate view parameter `" + parameter.name + "`");
                TypePtr type = resolve_type(parameter.type);
                if (type->kind == RuntimeType::Kind::table)
                    reject(parameter.type.span, "view parameters must be native Fine values");
                z3::expr term = context_.constant(
                    ("Fine.view." + declaration.name + ".arg" + std::to_string(i)).c_str(), type->sort);
                environment.emplace(parameter.name, TypedExpression{type, term});
                info.parameter_names.push_back(parameter.name);
                info.parameter_types.push_back(type);
            }
            ExpressionEnvironment witness_environment = environment;
            z3::expr value =
                context_.constant(("Fine.view." + declaration.name + ".value").c_str(), info.carrier->sort);
            environment.emplace("value", TypedExpression{info.carrier, value});

            bool previous_capture = capture_source_edges_;
            std::vector<std::string> previous_within = source_edge_within_;
            capture_source_edges_ = true;
            source_edge_within_ = {"view:" + declaration.name};
            z3::expr requirements = context_.bool_val(true);
            for (syntax::Expr const &requirement : declaration.requirements) {
                TypedExpression elaborated = elaborate_expression(requirement, environment);
                if (!same(elaborated.type, bool_type_))
                    reject(requirement.span, "a constrained-view requirement must have type Bool");
                requirements = requirements && elaborated.expression;
            }
            std::optional<z3::expr> witness;
            if (declaration.witness) {
                TypedExpression elaborated = elaborate_expression(*declaration.witness, witness_environment);
                if (!same(elaborated.type, info.carrier))
                    reject(declaration.witness->span,
                           "a constrained-view witness must have carrier type `" + info.carrier->display + "`");
                witness = elaborated.expression;
            }
            capture_source_edges_ = previous_capture;
            source_edge_within_ = std::move(previous_within);

            views_.emplace(declaration.name, std::move(info));
            if (rainfall_)
                rainfall_->record(
                    "object", "fine.view.declare", {"view:" + declaration.name}, "fine.elaborator",
                    "Named erased proposition over one existing native carrier; no wrapper sort is created",
                    {RainfallRecorder::string_field("view", declaration.name),
                     RainfallRecorder::string_field("carrier", views_.at(declaration.name).carrier->display),
                     RainfallRecorder::string_field("requirements", rainfall_->term(requirements)),
                     RainfallRecorder::string_field("availability", witness ? "declared-witness" : "solver-exists"),
                     RainfallRecorder::string_field("witness", witness ? rainfall_->term(*witness) : ""),
                     RainfallRecorder::number_field("parameters", declaration.parameters.size()),
                     RainfallRecorder::boolean_field("wrapper_sort", false)});
        }

        z3::expr Runtime::value(syntax::Expr const &expression, TypePtr const &expected) {
            switch (expression.kind) {
            case syntax::Expr::Kind::boolean:
                if (expected->kind != RuntimeType::Kind::boolean)
                    reject(expression.span, "Boolean value does not have type `" + expected->display + "`");
                return context_.bool_val(expression.boolean_value);
            case syntax::Expr::Kind::integer:
                if (expected->kind != RuntimeType::Kind::integer)
                    reject(expression.span, "integer value does not have type `" + expected->display + "`");
                return context_.int_val(expression.integer_text.c_str());
            case syntax::Expr::Kind::name: {
                if (expected->kind == RuntimeType::Kind::datatype) {
                    auto found = constructors_.find(expression.name);
                    if (found == constructors_.end())
                        reject(expression.span, "unknown constructor `" + expression.name + "`");
                    if (found->second.first != expected->datatype)
                        reject(expression.span, "constructor `" + expression.name + "` belongs to `" +
                                                    found->second.first->name + "`, not `" + expected->display + "`");
                    DatatypeCaseInfo const &item = found->second.first->cases[found->second.second];
                    if (!item.field_types.empty())
                        reject(expression.span, "constructor `" + expression.name + "` requires arguments");
                    return item.constructor();
                }
                if (expected->kind != RuntimeType::Kind::enumeration)
                    reject(expression.span,
                           "name `" + expression.name + "` is not a value of type `" + expected->display + "`");
                auto found = cases_.find(expression.name);
                if (found == cases_.end())
                    reject(expression.span, "unknown enum case `" + expression.name + "`");
                if (found->second.first != expected->enumeration)
                    reject(expression.span, "enum case `" + expression.name + "` belongs to `" +
                                                found->second.first->name + "`, not `" + expected->display + "`");
                return found->second.first->values[found->second.second];
            }
            case syntax::Expr::Kind::call: {
                if (expected->kind != RuntimeType::Kind::datatype)
                    reject(expression.span, "constructor call does not have type `" + expected->display + "`");
                auto found = constructors_.find(expression.name);
                if (found == constructors_.end())
                    reject(expression.span, "unknown constructor `" + expression.name + "`");
                if (found->second.first != expected->datatype)
                    reject(expression.span, "constructor `" + expression.name + "` belongs to `" +
                                                found->second.first->name + "`, not `" + expected->display + "`");
                DatatypeCaseInfo const &item = found->second.first->cases[found->second.second];
                if (expression.elements.size() != item.field_types.size())
                    reject(expression.span, "constructor `" + expression.name + "` expects " +
                                                std::to_string(item.field_types.size()) + " arguments");
                std::vector<z3::expr> arguments;
                arguments.reserve(expression.elements.size());
                for (std::size_t i = 0; i < expression.elements.size(); ++i)
                    arguments.push_back(value(expression.elements[i], item.field_types[i]));
                return item.constructor(static_cast<unsigned>(arguments.size()), arguments.data());
            }
            case syntax::Expr::Kind::tuple:
                if (expected->kind != RuntimeType::Kind::tuple)
                    reject(expression.span, "tuple does not have type `" + expected->display + "`");
                if (expression.elements.size() != 2)
                    reject(expression.span, "Fine v1 tuples contain exactly two values");
                return (*expected->tuple_constructor)(value(expression.elements[0], expected->arguments[0]),
                                                      value(expression.elements[1], expected->arguments[1]));
            case syntax::Expr::Kind::binary:
            case syntax::Expr::Kind::conditional:
            case syntax::Expr::Kind::hole:
                reject(expression.span, "computed expressions are not admitted as literal table cells");
            }
            reject(expression.span, "unknown Fine value form");
        }

        void Runtime::declare_let(syntax::LetDecl const &declaration) {
            reserve_value_name(declaration.name, declaration.span);
            TypePtr type = resolve_type(declaration.type);
            if (type->kind != RuntimeType::Kind::table)
                reject(declaration.type.span, "a `let` binding in this slice must be a Table");
            z3::expr result = table_value(type, declaration.value);
            bindings_.emplace(declaration.name, Binding{type, result, false, declaration.span});
        }

        z3::expr Runtime::table_value(TypePtr const &type, syntax::TableLiteral const &literal) {
            TypePtr const &domain = type->arguments[0];
            TypePtr const &range = type->arguments[1];
            z3::expr result = z3::const_array(domain->sort, value(literal.default_value, range));
            std::vector<z3::expr> keys;
            for (syntax::TableEntry const &entry : literal.entries) {
                z3::expr key = value(entry.key, domain);
                for (z3::expr const &previous : keys) {
                    if (Z3_is_eq_ast(context_, key, previous))
                        reject(entry.key.span, "duplicate table key");
                }
                keys.push_back(key);
                result = z3::store(result, key, value(entry.value, range));
            }
            return result;
        }

        void Runtime::declare_model(syntax::ModelDecl const &declaration) {
            reserve_value_name(declaration.name, declaration.span);
            TypePtr type = resolve_type(declaration.type);
            if (type->kind != RuntimeType::Kind::table)
                reject(declaration.type.span, "a `model` hole must have Table type");
            z3::expr hole = declaration.value ? table_value(type, *declaration.value)
                                              : context_.constant(declaration.name.c_str(), type->sort);
            bindings_.emplace(declaration.name, Binding{type, hole, !declaration.value, declaration.span});
        }

        Binding const &Runtime::binding(syntax::Expr const &expression, std::string const &role) const {
            if (expression.kind != syntax::Expr::Kind::name)
                reject(expression.span, role + " must name a table declaration");
            auto found = bindings_.find(expression.name);
            if (found == bindings_.end())
                reject(expression.span, "unknown table `" + expression.name + "`");
            return found->second;
        }

        bool Runtime::same(TypePtr const &left, TypePtr const &right) {
            return left.get() == right.get();
        }

        TypedExpression Runtime::elaborate_expression(syntax::Expr const &expression,
                                                      ExpressionEnvironment const &environment) {
            switch (expression.kind) {
            case syntax::Expr::Kind::name: {
                auto found = environment.find(expression.name);
                if (found != environment.end())
                    return completed_expression(expression, found->second.type, found->second.expression);
                auto enum_case = cases_.find(expression.name);
                if (enum_case != cases_.end())
                    return completed_expression(expression, enum_case->second.first->type,
                                                enum_case->second.first->values[enum_case->second.second]);
                auto constructor = constructors_.find(expression.name);
                if (constructor == constructors_.end())
                    reject(expression.span, "unknown value `" + expression.name + "`");
                DatatypeCaseInfo const &item = constructor->second.first->cases[constructor->second.second];
                if (!item.field_types.empty())
                    reject(expression.span, "constructor `" + expression.name + "` requires arguments");
                return completed_expression(expression, constructor->second.first->type, item.constructor());
            }
            case syntax::Expr::Kind::boolean:
                return completed_expression(expression, bool_type_, context_.bool_val(expression.boolean_value));
            case syntax::Expr::Kind::integer:
                return completed_expression(expression, int_type_, context_.int_val(expression.integer_text.c_str()));
            case syntax::Expr::Kind::tuple: {
                if (expression.elements.size() != 2)
                    reject(expression.span, "Fine v1 tuples contain exactly two values");
                TypedExpression first = elaborate_expression(expression.elements[0], environment);
                TypedExpression second = elaborate_expression(expression.elements[1], environment);
                TypePtr type = tuple_type(first.type, second.type, expression.span);
                return completed_expression(expression, type,
                                            (*type->tuple_constructor)(first.expression, second.expression));
            }
            case syntax::Expr::Kind::call: {
                auto found = constructors_.find(expression.name);
                if (found == constructors_.end()) {
                    auto proof_family = proof_families_.find(expression.name);
                    if (proof_family != proof_families_.end()) {
                        ProofFamilyInfo const &item = *proof_family->second;
                        if (expression.elements.size() != item.index_types.size())
                            reject(expression.span, "proof family `" + expression.name + "` expects " +
                                                        std::to_string(item.index_types.size()) + " indices");
                        std::vector<z3::expr> indices;
                        indices.reserve(expression.elements.size());
                        for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                            TypedExpression index = elaborate_expression(expression.elements[i], environment);
                            if (!same(index.type, item.index_types[i]))
                                reject(expression.elements[i].span, "index " + std::to_string(i + 1) + " of `" +
                                                                        item.name + "` must have type `" +
                                                                        item.index_types[i]->display + "`");
                            indices.push_back(index.expression);
                        }
                        return completed_expression(
                            expression, bool_type_,
                            item.relation(static_cast<unsigned>(indices.size()), indices.data()));
                    }
                    auto function = functions_.find(expression.name);
                    if (function == functions_.end())
                        reject(expression.span, "unknown constructor or function `" + expression.name + "`");
                    FunctionInfo const &item = *function->second;
                    if (expression.elements.size() != item.parameter_types.size())
                        reject(expression.span, "function `" + expression.name + "` expects " +
                                                    std::to_string(item.parameter_types.size()) + " arguments");
                    std::vector<z3::expr> arguments;
                    arguments.reserve(expression.elements.size());
                    for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                        TypedExpression argument = elaborate_expression(expression.elements[i], environment);
                        if (!same(argument.type, item.parameter_types[i]))
                            reject(expression.elements[i].span, "argument " + std::to_string(i + 1) + " of `" +
                                                                    item.name + "` must have type `" +
                                                                    item.parameter_types[i]->display + "`");
                        arguments.push_back(argument.expression);
                    }
                    return completed_expression(
                        expression, item.result_type,
                        item.declaration(static_cast<unsigned>(arguments.size()), arguments.data()));
                }
                DatatypeInfo *datatype = found->second.first;
                DatatypeCaseInfo const &item = datatype->cases[found->second.second];
                if (expression.elements.size() != item.field_types.size())
                    reject(expression.span, "constructor `" + expression.name + "` expects " +
                                                std::to_string(item.field_types.size()) + " arguments");
                std::vector<z3::expr> arguments;
                arguments.reserve(expression.elements.size());
                for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                    TypedExpression argument = elaborate_expression(expression.elements[i], environment);
                    if (!same(argument.type, item.field_types[i]))
                        reject(expression.elements[i].span, "argument `" + item.field_names[i] + "` of `" + item.name +
                                                                "` must have type `" + item.field_types[i]->display +
                                                                "`");
                    arguments.push_back(argument.expression);
                }
                return completed_expression(
                    expression, datatype->type,
                    item.constructor(static_cast<unsigned>(arguments.size()), arguments.data()));
            }
            case syntax::Expr::Kind::binary: {
                if (expression.elements.size() != 2)
                    throw std::runtime_error("internal Fine binary expression arity");
                TypedExpression left = elaborate_expression(expression.elements[0], environment);
                TypedExpression right = elaborate_expression(expression.elements[1], environment);
                switch (expression.binary_op) {
                case syntax::Expr::BinaryOp::equal:
                    if (!same(left.type, right.type) || left.type->kind == RuntimeType::Kind::table)
                        reject(expression.span, "both sides of `==` must have the same value type");
                    return completed_expression(expression, bool_type_, left.expression == right.expression);
                case syntax::Expr::BinaryOp::greater_equal:
                case syntax::Expr::BinaryOp::less_equal:
                    if (!same(left.type, int_type_) || !same(right.type, int_type_))
                        reject(expression.span, "ordered comparison requires two Int values");
                    return completed_expression(expression, bool_type_,
                                                expression.binary_op == syntax::Expr::BinaryOp::greater_equal
                                                    ? left.expression >= right.expression
                                                    : left.expression <= right.expression);
                case syntax::Expr::BinaryOp::logical_and:
                case syntax::Expr::BinaryOp::logical_or:
                    if (!same(left.type, bool_type_) || !same(right.type, bool_type_))
                        reject(expression.span, "Boolean connective requires two Bool values");
                    return completed_expression(expression, bool_type_,
                                                expression.binary_op == syntax::Expr::BinaryOp::logical_and
                                                    ? left.expression && right.expression
                                                    : left.expression || right.expression);
                case syntax::Expr::BinaryOp::add:
                case syntax::Expr::BinaryOp::subtract:
                    if (!same(left.type, int_type_) || !same(right.type, int_type_))
                        reject(expression.span, "integer arithmetic requires two Int values");
                    return completed_expression(expression, int_type_,
                                                expression.binary_op == syntax::Expr::BinaryOp::add
                                                    ? left.expression + right.expression
                                                    : left.expression - right.expression);
                }
                throw std::runtime_error("internal Fine binary operator");
            }
            case syntax::Expr::Kind::conditional: {
                if (expression.elements.size() != 3)
                    throw std::runtime_error("internal Fine conditional arity");
                TypedExpression condition = elaborate_expression(expression.elements[0], environment);
                TypedExpression yes = elaborate_expression(expression.elements[1], environment);
                TypedExpression no = elaborate_expression(expression.elements[2], environment);
                if (!same(condition.type, bool_type_))
                    reject(expression.elements[0].span, "an `if` condition must have type Bool");
                if (!same(yes.type, no.type))
                    reject(expression.span, "both `if` branches must have the same type");
                return completed_expression(expression, yes.type,
                                            z3::ite(condition.expression, yes.expression, no.expression));
            }
            case syntax::Expr::Kind::hole:
                reject(expression.span, "a hole is admitted only as an entire synthesis match arm");
            }
            throw std::runtime_error("internal Fine expression kind");
        }

        syntax::Expr Runtime::lift_expression(z3::expr const &expression,
                                              std::vector<std::pair<std::string, z3::expr>> const &parameters) const {
            for (auto const &[name, parameter] : parameters) {
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
            default: reject({}, "lift encountered an operation outside the admitted synth body");
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

        syntax::Expr Runtime::lift_typed_expression(z3::expr const &expression, TypePtr const &type) const {
            if (type->kind == RuntimeType::Kind::boolean) {
                if (!expression.is_true() && !expression.is_false())
                    reject({}, "lift encountered a non-literal Bool model value");
                syntax::Expr result;
                result.kind = syntax::Expr::Kind::boolean;
                result.boolean_value = expression.is_true();
                return result;
            }
            if (type->kind == RuntimeType::Kind::integer) {
                std::string numeral;
                if (!expression.is_numeral(numeral))
                    reject({}, "lift encountered a non-numeral Int model value");
                syntax::Expr result;
                result.kind = syntax::Expr::Kind::integer;
                result.integer_text = std::move(numeral);
                return result;
            }
            if (type->kind == RuntimeType::Kind::enumeration) {
                for (unsigned i = 0; i < type->enumeration->values.size(); ++i) {
                    if (Z3_is_eq_ast(context_, expression, type->enumeration->values[i])) {
                        syntax::Expr result;
                        result.kind = syntax::Expr::Kind::name;
                        result.name = type->enumeration->case_names[i];
                        return result;
                    }
                }
                reject({}, "lift encountered a value outside enum `" + type->display + "`");
            }
            if (type->kind == RuntimeType::Kind::datatype) {
                if (!expression.is_app())
                    reject({}, "lift encountered a non-constructor value for `" + type->display + "`");
                for (DatatypeCaseInfo const &item : type->datatype->cases) {
                    if (!Z3_is_eq_func_decl(context_, expression.decl(), item.constructor))
                        continue;
                    if (expression.num_args() != item.field_types.size())
                        throw std::runtime_error("internal datatype constructor arity mismatch");
                    syntax::Expr result;
                    result.kind = item.field_types.empty() ? syntax::Expr::Kind::name : syntax::Expr::Kind::call;
                    result.name = item.name;
                    for (unsigned i = 0; i < expression.num_args(); ++i)
                        result.elements.push_back(lift_typed_expression(expression.arg(i), item.field_types[i]));
                    return result;
                }
                reject({}, "lift encountered a value outside datatype `" + type->display + "`");
            }
            if (type->kind == RuntimeType::Kind::tuple) {
                if (!expression.is_app() || expression.num_args() != 2 ||
                    !Z3_is_eq_func_decl(context_, expression.decl(), *type->tuple_constructor))
                    reject({}, "lift encountered a value outside tuple `" + type->display + "`");
                syntax::Expr result;
                result.kind = syntax::Expr::Kind::tuple;
                result.elements.push_back(lift_typed_expression(expression.arg(0), type->arguments[0]));
                result.elements.push_back(lift_typed_expression(expression.arg(1), type->arguments[1]));
                return result;
            }
            reject({}, "lift does not admit values of type `" + type->display + "`");
        }

        char const *Runtime::operator_text(syntax::Expr::BinaryOp operation) {
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

        void Runtime::print_expression(std::ostream &output, syntax::Expr const &expression) {
            switch (expression.kind) {
            case syntax::Expr::Kind::name: output << expression.name; return;
            case syntax::Expr::Kind::boolean: output << (expression.boolean_value ? "true" : "false"); return;
            case syntax::Expr::Kind::integer: output << expression.integer_text; return;
            case syntax::Expr::Kind::call:
                output << expression.name << '(';
                for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                    if (i)
                        output << ", ";
                    print_expression(output, expression.elements[i]);
                }
                output << ')';
                return;
            case syntax::Expr::Kind::tuple:
                output << '(';
                for (std::size_t i = 0; i < expression.elements.size(); ++i) {
                    if (i)
                        output << ", ";
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
            case syntax::Expr::Kind::hole: output << '?' << expression.name; return;
            }
        }

        int Runtime::execute_check(syntax::CheckDecl const &declaration) {
            if (!proof_families_.empty())
                return execute_proof_family_check(declaration);
            if (declaration.proof_induction)
                reject(declaration.proof_induction->span, "proof-family induction target has no declared proof family");
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
                syntax::Parameter const &parameter = declaration.parameters[i];
                TypePtr type = resolve_type(parameter.type);
                if (!same(type, int_type_) && !same(type, bool_type_) && type->kind != RuntimeType::Kind::enumeration &&
                    type->kind != RuntimeType::Kind::datatype && type->kind != RuntimeType::Kind::tuple)
                    reject(parameter.type.span, "check parameters admit Int, Bool, enums, datatypes, and tuples");
                std::string internal = "Fine.check." + declaration.name + ".arg" + std::to_string(i);
                z3::expr term = context_.constant(internal.c_str(), type->sort);
                if (!environment.emplace(parameter.name, TypedExpression{type, term}).second)
                    reject(parameter.span, "duplicate parameter `" + parameter.name + "`");
                parameters.push_back({parameter.name, type, term});
            }

            auto conjunction = [&](std::vector<syntax::Expr> const &conditions, std::string_view role) {
                z3::expr result = context_.bool_val(true);
                for (syntax::Expr const &condition : conditions) {
                    TypedExpression elaborated = elaborate_expression(condition, environment);
                    if (!same(elaborated.type, bool_type_))
                        reject(condition.span, std::string(role) + " condition must have type Bool");
                    result = result && elaborated.expression;
                }
                return result;
            };
            std::string run_scope = "check:" + declaration.name;
            std::string query = "query:0";
            if (rainfall_)
                rainfall_->record("scope", "check.run.open", {run_scope}, "fine.check",
                                  "Fine check elaboration, one public counterexample query, and optional "
                                  "source-witness round trip; induction checks additionally expose the public "
                                  "qi_queue binding and on-clause boundaries, not the rest of Z3 search",
                                  {RainfallRecorder::string_field("declaration", declaration.name),
                                   RainfallRecorder::number_field("parameters", parameters.size())});

            capture_source_edges_ = true;
            source_edge_within_ = {run_scope};
            z3::expr assumptions = conjunction(declaration.assumes, "assumed");
            z3::expr guarantees = conjunction(declaration.ensures, "ensured");
            capture_source_edges_ = false;
            source_edge_within_.clear();
            z3::expr theorem = z3::implies(assumptions, guarantees);
            z3::expr induction_hypothesis = context_.bool_val(true);
            z3::expr induction_step = theorem;
            std::string induction_parameter;
            if (declaration.induction_parameter) {
                induction_parameter = *declaration.induction_parameter;
                auto found = std::find_if(parameters.begin(), parameters.end(),
                                          [&](CheckParameter const &item) { return item.name == induction_parameter; });
                if (found == parameters.end())
                    reject(*declaration.induction_span, "unknown induction parameter `" + induction_parameter + "`");
                if (found->type->kind != RuntimeType::Kind::datatype)
                    reject(*declaration.induction_span,
                           "direct-subterm induction requires a field-bearing datatype parameter");

                DatatypeInfo const &datatype = *found->type->datatype;
                std::string smaller_name = "Fine.check." + declaration.name + ".smaller";
                z3::expr smaller = context_.constant(smaller_name.c_str(), found->type->sort);
                z3::expr direct_subterm = context_.bool_val(false);
                std::size_t recursive_positions = 0;
                for (DatatypeCaseInfo const &item : datatype.cases) {
                    z3::expr is_case = item.recognizer(found->term);
                    for (std::size_t i = 0; i < item.field_types.size(); ++i) {
                        if (!same(item.field_types[i], found->type))
                            continue;
                        direct_subterm = direct_subterm || (is_case && smaller == item.accessors[i](found->term));
                        ++recursive_positions;
                    }
                }
                if (recursive_positions == 0)
                    reject(*declaration.induction_span, "the induction datatype has no direct recursive fields");

                z3::expr_vector source(context_);
                z3::expr_vector destination(context_);
                source.push_back(found->term);
                destination.push_back(smaller);
                z3::expr smaller_theorem = theorem.substitute(source, destination);
                z3::expr hypothesis_body = z3::implies(direct_subterm, smaller_theorem);
                std::vector<Z3_app> bound{reinterpret_cast<Z3_app>(static_cast<Z3_ast>(smaller))};
                std::string qid = "fine.induction." + declaration.name + "." + induction_parameter;
                Z3_ast quantified = Z3_mk_quantifier_const_ex(
                    context_, true, 0, context_.str_symbol(qid.c_str()), context_.str_symbol(""),
                    static_cast<unsigned>(bound.size()), bound.data(), 0, nullptr, 0, nullptr, hypothesis_body);
                context_.check_error();
                induction_hypothesis = z3::expr(context_, quantified);
                induction_step = z3::implies(induction_hypothesis, theorem);

                if (rainfall_) {
                    rainfall_->source_term(declaration.node_id, declaration.span, "decl.check", induction_step,
                                           "generated", {run_scope});
                    rainfall_->record(
                        "transform", "check.induction.translate", {run_scope}, "fine.elaborator",
                        "Compiler-owned weak structural induction translation over direct recursive datatype "
                        "fields; Z3 receives only the resulting ordinary quantified formula",
                        {RainfallRecorder::string_field("parameter", induction_parameter),
                         RainfallRecorder::string_field("order", "direct-subterm"),
                         RainfallRecorder::number_field("recursive_positions", recursive_positions),
                         RainfallRecorder::string_field("theorem", rainfall_->term(theorem)),
                         RainfallRecorder::string_field("hypothesis", rainfall_->term(induction_hypothesis)),
                         RainfallRecorder::string_field("step", rainfall_->term(induction_step)),
                         RainfallRecorder::string_field("responsibility", "fine-generated-induction-scheme")});
                }
            }
            z3::expr counterexample_query = !induction_step;

            if (rainfall_) {
                rainfall_->record(
                    "constraint", "check.counterexample.assert", {run_scope}, "fine.check",
                    declaration.induction_parameter
                        ? "Negation of Fine's compiler-generated direct-subterm induction step"
                        : "Conjunction of source assumptions and the negation of all source guarantees",
                    {RainfallRecorder::string_field("assumptions", rainfall_->term(assumptions)),
                     RainfallRecorder::string_field("guarantees", rainfall_->term(guarantees)),
                     RainfallRecorder::string_field("theorem", rainfall_->term(theorem)),
                     RainfallRecorder::string_field("induction_hypothesis", rainfall_->term(induction_hypothesis)),
                     RainfallRecorder::string_field("assertion", rainfall_->term(counterexample_query))});
                rainfall_->record(
                    "scope", "solver.query.open", {run_scope, query}, "fine.check",
                    declaration.induction_parameter
                        ? "Public solver assertion boundary with scoped read-only E-matching binding and "
                          "on-clause observers"
                        : "Public solver assertion boundary",
                    {RainfallRecorder::string_field("id", query),
                     RainfallRecorder::string_field("purpose",
                                                    declaration.induction_parameter
                                                        ? "refute the compiler-generated structural induction step"
                                                        : "find a source-level counterexample"),
                     RainfallRecorder::string_field("assertion", rainfall_->term(counterexample_query)),
                     RainfallRecorder::string_field("polarity", "counterexample-exists"),
                     RainfallRecorder::boolean_field("induction_translation",
                                                     declaration.induction_parameter.has_value()),
                     RainfallRecorder::boolean_field("mbqi", !declaration.induction_parameter.has_value()),
                     RainfallRecorder::boolean_field("ematching", true)});
            }

            z3::solver solver(context_);
            if (declaration.induction_parameter) {
                z3::params parameters(context_);
                parameters.set("mbqi", false);
                parameters.set("ematching", true);
                parameters.set("rewriter.enable_der", false);
                solver.set(parameters);
            }
            solver.add(counterexample_query);
            std::unique_ptr<RainfallQuantifierObserver> quantifier_observer;
            std::unique_ptr<RainfallClauseObserver> clause_observer;
            if (rainfall_ && declaration.induction_parameter) {
                quantifier_observer = std::make_unique<RainfallQuantifierObserver>(
                    solver, *rainfall_, std::vector<std::string>{run_scope, query}, true, false);
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
                    "Final public check result only; no claim about solver search or internal cause",
                    {RainfallRecorder::string_field("query", query), RainfallRecorder::string_field("status", status),
                     RainfallRecorder::string_field("polarity", "counterexample-exists"),
                     RainfallRecorder::string_field("domain_outcome", result == z3::sat     ? "refuted"
                                                                      : result == z3::unsat ? "verified"
                                                                                            : "unknown")});
                rainfall_->record("scope", "solver.query.close", {run_scope, query}, "fine.check",
                                  "Public solver query lifetime", {RainfallRecorder::string_field("id", query)});
            }
            if (result == z3::unknown)
                reject(declaration.span, "counterexample query was unknown: " + solver.reason_unknown());
            if (result == z3::unsat) {
                if (rainfall_) {
                    rainfall_->validate_terms();
                    rainfall_->record("scope", "check.run.close", {run_scope}, "fine.check",
                                      "Source check completed with no counterexample",
                                      {RainfallRecorder::string_field("status", "verified")});
                }
                output_ << "verified: " << declaration.name << '\n';
                if (declaration.induction_parameter)
                    output_ << "induction: direct-subterm on " << induction_parameter << '\n';
                output_ << "counterexample: none\n";
                return 0;
            }

            z3::model model = solver.get_model();
            std::vector<z3::expr> values;
            values.reserve(parameters.size());
            std::ostringstream rendered;
            rendered << "counterexample " << declaration.name << " {\n";
            for (CheckParameter const &parameter : parameters) {
                z3::expr value = model.eval(parameter.term, true);
                syntax::Expr lifted = lift_typed_expression(value, parameter.type);
                values.push_back(value);
                rendered << "  " << parameter.name << ": " << parameter.type->display << " = ";
                print_expression(rendered, lifted);
                rendered << ";\n";
                if (rainfall_)
                    rainfall_->record(
                        "derive", "model.eval-assignment", {run_scope}, "z3.public-api",
                        "Completed evaluation of one check parameter under the returned counterexample model",
                        {RainfallRecorder::string_field("evidence_query", query),
                         RainfallRecorder::string_field("parameter", parameter.name),
                         RainfallRecorder::string_field("term", rainfall_->term(parameter.term)),
                         RainfallRecorder::string_field("value", rainfall_->term(value)),
                         RainfallRecorder::boolean_field("model_completion", true),
                         RainfallRecorder::string_field("relation", "equality-under-this-model")});
            }
            rendered << "}\n";
            std::string witness_source = rendered.str();

            syntax::Document witness_document = syntax::parse(witness_source);
            if (witness_document.declarations.size() != 1)
                throw std::runtime_error("internal counterexample parser returned extra declarations");
            auto const *witness = std::get_if<syntax::CounterexampleDecl>(&witness_document.declarations.front());
            if (!witness || witness->name != declaration.name || witness->entries.size() != parameters.size())
                throw std::runtime_error("internal counterexample parser changed the witness");
            ExpressionEnvironment empty_environment;
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                syntax::CounterexampleEntry const &entry = witness->entries[i];
                CheckParameter const &parameter = parameters[i];
                if (entry.name != parameter.name)
                    throw std::runtime_error("counterexample parser changed a parameter name");
                TypePtr parsed_type = resolve_type(entry.type);
                TypedExpression roundtrip = elaborate_expression(entry.value, empty_environment);
                if (!same(parsed_type, parameter.type) || !same(roundtrip.type, parameter.type) ||
                    !Z3_is_eq_ast(context_, roundtrip.expression, values[i]))
                    reject(entry.span, "parse(print(lift(value))) violated exact AST identity");
            }

            if (rainfall_) {
                rainfall_->record("object", "fine.counterexample-witness", {run_scope}, "fine.runtime",
                                  "Lifted, printed, parsed, and elaborated admitted value assignments with exact "
                                  "same-manager AST identity",
                                  {RainfallRecorder::string_field("declaration", declaration.name),
                                   RainfallRecorder::string_field("source", witness_source),
                                   RainfallRecorder::boolean_field("parse_reify_exact_identity", true)});
                rainfall_->record("transition", "fine.witness.accept", {run_scope}, "fine.runtime",
                                  "Satisfiable counterexample query plus Fine source round-trip identity check",
                                  {RainfallRecorder::string_field("declaration", declaration.name),
                                   RainfallRecorder::string_field("evidence_query", query),
                                   RainfallRecorder::string_field("status", "counterexample-witness"),
                                   RainfallRecorder::boolean_field("source_roundtrip_exact_identity", true)});
                rainfall_->validate_terms();
                rainfall_->record("scope", "check.run.close", {run_scope}, "fine.runtime",
                                  "Source check completed with a returned counterexample",
                                  {RainfallRecorder::string_field("status", "counterexample-witness")});
            }

            output_ << "refuted: " << declaration.name << '\n';
            if (declaration.induction_parameter)
                output_ << "induction: direct-subterm on " << induction_parameter << '\n';
            output_ << witness_source;
            output_ << "parse(print(lift(values))): exact ast identity\n";
            return 0;
        }

        int Runtime::execute(syntax::Document const &document) {
            declare_document_sources(document);
            syntax::ProofDecl const *proof = nullptr;
            syntax::SynthDecl const *synth = nullptr;
            syntax::CheckDecl const *check = nullptr;
            syntax::ModelDecl const *model_hole = nullptr;
            for (syntax::Declaration const &declaration : document.declarations) {
                if (auto const *item = std::get_if<syntax::EnumDecl>(&declaration)) {
                    declare_enum(*item);
                }
                else if (auto const *item = std::get_if<syntax::FunctionDecl>(&declaration)) {
                    declare_function(*item);
                }
                else if (auto const *item = std::get_if<syntax::LetDecl>(&declaration)) {
                    declare_let(*item);
                }
                else if (auto const *item = std::get_if<syntax::ModelDecl>(&declaration)) {
                    declare_model(*item);
                    if (!item->value) {
                        if (model_hole)
                            reject(item->span, "this proof slice admits exactly one model-shaped hole");
                        model_hole = item;
                    }
                }
                else if (auto const *item = std::get_if<syntax::ProofDecl>(&declaration)) {
                    if (proof || synth || check)
                        reject(item->span, "this source slice admits one executable declaration");
                    proof = item;
                }
                else if (auto const *item = std::get_if<syntax::ViewDecl>(&declaration)) {
                    declare_view(*item);
                }
                else if (auto const *item = std::get_if<syntax::ProofFamilyDecl>(&declaration)) {
                    declare_proof_family(*item);
                }
                else if (auto const *item = std::get_if<syntax::SynthDecl>(&declaration)) {
                    if (proof || synth || check)
                        reject(item->span, "this source slice admits one executable declaration");
                    synth = item;
                }
                else if (auto const *item = std::get_if<syntax::CheckDecl>(&declaration)) {
                    if (proof || synth || check)
                        reject(item->span, "this source slice admits one executable declaration");
                    check = item;
                }
                else if (auto const *item = std::get_if<syntax::CounterexampleDecl>(&declaration)) {
                    reject(item->span, "a `counterexample` declaration is a returned witness, not an executable check");
                }
            }
            if (!proof_families_.empty() && !check)
                reject(document.span, "proof families currently require one ground least-relation membership `check`");
            if (synth)
                return execute_synthesis(*synth);
            if (check)
                return execute_check(*check);
            if (!proof)
                reject(document.span, "expected one `proof`, `synth`, or `check` declaration");
            if (!model_hole)
                reject(document.span, "expected one model-shaped hole for the proof result");
            return execute_bisimulation(*proof);
        }

    }  // namespace runtime_detail

    SemanticError::SemanticError(syntax::SourceSpan span, std::string message)
        : std::runtime_error(std::move(message)), span_(span) {}

    std::string SemanticError::format(std::string_view filename, std::string_view source) const {
        std::size_t offset = std::min(span_.begin.offset, source.size());
        std::size_t line_begin = offset;
        while (line_begin > 0 && source[line_begin - 1] != '\n')
            --line_begin;
        std::size_t line_end = offset;
        while (line_end < source.size() && source[line_end] != '\n')
            ++line_end;
        std::string_view line = source.substr(line_begin, line_end - line_begin);
        std::size_t column = std::max<std::size_t>(1, span_.begin.column);
        std::size_t width = std::max<std::size_t>(1, span_.end.offset - span_.begin.offset);

        std::ostringstream output;
        output << filename << ':' << std::max<std::size_t>(1, span_.begin.line) << ':' << column
               << ": error: " << what() << '\n'
               << line << '\n'
               << std::string(column - 1, ' ') << '^';
        if (width > 1 && width <= line.size())
            output << std::string(width - 1, '~');
        return output.str();
    }

    int execute(syntax::Document const &document, std::ostream &output, std::ostream *rainfall_output,
                SourceSnapshot const *snapshot, std::string rainfall_run) {
        return runtime_detail::Runtime(output, rainfall_output, snapshot, std::move(rainfall_run)).execute(document);
    }

}  // namespace fine
