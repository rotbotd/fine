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

namespace fine {
    namespace {

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

        struct ProofConstructorInfo {
            struct ArbitraryField {
                std::string binder;
                std::string view_name;
                z3::expr binder_term;
                z3::expr requirement;
                std::vector<z3::expr> premise_terms;
                std::vector<std::vector<z3::expr>> recursive_premise_indices;

                explicit ArbitraryField(z3::context &context)
                    : binder_term(context), requirement(context) {}
            };

            std::string name;
            std::vector<z3::expr> parameters;
            std::vector<z3::expr> result_indices;
            std::vector<std::vector<z3::expr>> recursive_premise_indices;
            std::vector<ArbitraryField> arbitrary_fields;
            std::size_t premise_count = 0;
        };

        struct ProofFamilyInfo {
            std::string name;
            std::vector<TypePtr> index_types;
            z3::func_decl relation;
            std::vector<ProofConstructorInfo> constructors;
            bool horn_complete = true;

            explicit ProofFamilyInfo(z3::context &context) : relation(context) {}
        };

        struct ViewInfo {
            std::string name;
            std::vector<std::string> parameter_names;
            std::vector<TypePtr> parameter_types;
            TypePtr carrier;
            std::vector<syntax::Expr> requirements;
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

        [[noreturn]] void reject(syntax::SourceSpan span, std::string message) {
            throw SemanticError(span, std::move(message));
        }

        class Runtime {
        public:
            explicit Runtime(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                             std::string rainfall_run)
                : output_(output),
                  fixedpoint_(context_),
                  rainfall_(rainfall_output ? std::make_unique<RainfallRecorder>(context_, *rainfall_output,
                                                                                 std::move(rainfall_run), snapshot)
                                            : nullptr),
                  bool_type_(std::make_shared<RuntimeType>(RuntimeType::Kind::boolean, context_.bool_sort(), "Bool")),
                  int_type_(std::make_shared<RuntimeType>(RuntimeType::Kind::integer, context_.int_sort(), "Int")) {
                types_.emplace("Bool", bool_type_);
                types_.emplace("Int", int_type_);
            }

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
            std::map<std::string, std::unique_ptr<ProofFamilyInfo>> proof_families_;
            std::map<std::string, Binding> bindings_;
            std::map<std::string, TypePtr> compound_types_;
            std::set<std::string> value_names_;
            unsigned tuple_sequence_ = 0;
            bool capture_source_edges_ = false;
            std::vector<std::string> source_edge_within_;

            static char const *expression_syntax_kind(syntax::Expr const &expression) {
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

            static char const *expression_correspondence(syntax::Expr const &expression) {
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

            TypedExpression completed_expression(syntax::Expr const &source, TypePtr type, z3::expr expression) {
                if (rainfall_ && capture_source_edges_)
                    rainfall_->source_term(source.node_id, source.span, expression_syntax_kind(source), expression,
                                           expression_correspondence(source), source_edge_within_);
                return {std::move(type), std::move(expression)};
            }

            void declare_expression_sources(syntax::Expr const &expression) {
                rainfall_->source_node(expression.node_id, expression.span, expression_syntax_kind(expression));
                for (syntax::Expr const &child : expression.elements)
                    declare_expression_sources(child);
            }

            void declare_table_sources(syntax::TableLiteral const &table) {
                declare_expression_sources(table.default_value);
                for (syntax::TableEntry const &entry : table.entries) {
                    declare_expression_sources(entry.key);
                    declare_expression_sources(entry.value);
                }
            }

            void declare_document_sources(syntax::Document const &document) {
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
                            }
                            else if constexpr (std::is_same_v<T, syntax::ProofFamilyDecl>) {
                                for (syntax::ProofConstructor const &constructor : item.constructors) {
                                    for (syntax::Expr const &premise : constructor.premises)
                                        declare_expression_sources(premise);
                                    for (syntax::ArbitraryPremise const &field : constructor.arbitrary_premises) {
                                        rainfall_->source_node(field.node_id, field.span,
                                                               "proof.arbitrary-field");
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

            void reserve_type_name(std::string const &name, syntax::SourceSpan span) {
                if (types_.contains(name))
                    reject(span, "type `" + name + "` is already declared");
            }

            void reserve_value_name(std::string const &name, syntax::SourceSpan span) {
                if (!value_names_.insert(name).second)
                    reject(span, "value `" + name + "` is already declared");
            }

            void declare_enum(syntax::EnumDecl const &declaration) {
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

            void declare_datatype(syntax::EnumDecl const &declaration) {
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
                                        static_cast<unsigned>(field_names.size()), field_names.data(),
                                        field_sorts.data());
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
                    z3_constructors.query(static_cast<unsigned>(i), case_info.constructor, case_info.recognizer,
                                          accessors);
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

            void declare_function(syntax::FunctionDecl const &declaration) {
                reserve_value_name(declaration.name, declaration.span);
                if (declaration.parameters.empty())
                    reject(declaration.span, "a function needs at least one parameter");
                if (declaration.scrutinee.kind != syntax::Expr::Kind::name)
                    reject(declaration.scrutinee.span,
                           "the first function slice matches one named parameter directly");

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
                info->declaration = context_.recfun(declaration.name.c_str(),
                                                    static_cast<unsigned>(parameter_sorts.size()),
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
                        reject(arm.span, "constructor `" + arm.constructor + "` does not belong to `" +
                                             datatype->name + "`");
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
                        arm_environment.emplace(
                            binding.name,
                            TypedExpression{item.field_types[i], item.accessors[i](matched_term)});
                        if (same(item.field_types[i], matched_type))
                            recursive_bindings.insert(binding.name);
                    }
                    std::function<void(syntax::Expr const &)> check_structural_calls =
                        [&](syntax::Expr const &expression) {
                            if (expression.kind == syntax::Expr::Kind::call &&
                                expression.name == declaration.name) {
                                if (expression.elements.size() != declaration.parameters.size())
                                    reject(expression.span, "recursive call to `" + declaration.name +
                                                                "` has the wrong arity");
                                syntax::Expr const &decreasing = expression.elements[matched_parameter_index];
                                if (decreasing.kind != syntax::Expr::Kind::name ||
                                    !recursive_bindings.contains(decreasing.name))
                                    reject(decreasing.span,
                                           "recursive call to `" + declaration.name + "` must pass a direct `" +
                                               datatype->name + "` pattern field as its matched argument");
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
                    rainfall_->source_term(declaration.node_id, declaration.span, "decl.function", body,
                                           "generated", {scope});
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

            TypePtr tuple_type(TypePtr const &first, TypePtr const &second, syntax::SourceSpan span) {
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

            TypePtr resolve_type(syntax::Type const &syntax_type) {
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

            void declare_view(syntax::ViewDecl const &declaration) {
                if (views_.contains(declaration.name) || types_.contains(declaration.name) ||
                    functions_.contains(declaration.name) || proof_families_.contains(declaration.name))
                    reject(declaration.span, "duplicate type-level name `" + declaration.name + "`");

                ViewInfo info;
                info.name = declaration.name;
                info.carrier = resolve_type(declaration.carrier);
                if (info.carrier->kind == RuntimeType::Kind::table)
                    reject(declaration.carrier.span,
                           "a constrained view must keep one existing native Fine carrier");
                info.requirements = declaration.requirements;

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
                z3::expr value = context_.constant(("Fine.view." + declaration.name + ".value").c_str(),
                                                   info.carrier->sort);
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
                         RainfallRecorder::number_field("parameters", declaration.parameters.size()),
                         RainfallRecorder::boolean_field("wrapper_sort", false)});
            }

            z3::expr value(syntax::Expr const &expression, TypePtr const &expected) {
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
                                                        found->second.first->name + "`, not `" + expected->display +
                                                        "`");
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

            void declare_let(syntax::LetDecl const &declaration) {
                reserve_value_name(declaration.name, declaration.span);
                TypePtr type = resolve_type(declaration.type);
                if (type->kind != RuntimeType::Kind::table)
                    reject(declaration.type.span, "a `let` binding in this slice must be a Table");
                z3::expr result = table_value(type, declaration.value);
                bindings_.emplace(declaration.name, Binding{type, result, false, declaration.span});
            }

            z3::expr table_value(TypePtr const &type, syntax::TableLiteral const &literal) {
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

            void declare_model(syntax::ModelDecl const &declaration) {
                reserve_value_name(declaration.name, declaration.span);
                TypePtr type = resolve_type(declaration.type);
                if (type->kind != RuntimeType::Kind::table)
                    reject(declaration.type.span, "a `model` hole must have Table type");
                z3::expr hole = declaration.value ? table_value(type, *declaration.value)
                                                  : context_.constant(declaration.name.c_str(), type->sort);
                bindings_.emplace(declaration.name, Binding{type, hole, !declaration.value, declaration.span});
            }

            Binding const &binding(syntax::Expr const &expression, std::string const &role) const {
                if (expression.kind != syntax::Expr::Kind::name)
                    reject(expression.span, role + " must name a table declaration");
                auto found = bindings_.find(expression.name);
                if (found == bindings_.end())
                    reject(expression.span, "unknown table `" + expression.name + "`");
                return found->second;
            }

            static bool same(TypePtr const &left, TypePtr const &right) {
                return left.get() == right.get();
            }

            using ExpressionEnvironment = std::map<std::string, TypedExpression>;

            TypedExpression elaborate_expression(syntax::Expr const &expression,
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
                    return completed_expression(expression, int_type_,
                                                context_.int_val(expression.integer_text.c_str()));
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
                                    reject(expression.elements[i].span,
                                           "index " + std::to_string(i + 1) + " of `" + item.name +
                                               "` must have type `" + item.index_types[i]->display + "`");
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
                            reject(expression.elements[i].span, "argument `" + item.field_names[i] + "` of `" +
                                                                    item.name + "` must have type `" +
                                                                    item.field_types[i]->display + "`");
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

            void declare_proof_family(syntax::ProofFamilyDecl const &declaration) {
                reserve_value_name(declaration.name, declaration.span);
                std::vector<TypePtr> index_types;
                std::vector<z3::sort> index_sorts;
                std::set<std::string> index_names;
                for (syntax::Parameter const &index : declaration.indices) {
                    if (!index_names.insert(index.name).second)
                        reject(index.span, "duplicate proof-family index `" + index.name + "`");
                    TypePtr type = resolve_type(index.type);
                    if (type->kind == RuntimeType::Kind::table)
                        reject(index.type.span, "proof-family indices must be native Fine values, not Table");
                    index_types.push_back(type);
                    index_sorts.push_back(type->sort);
                }

                auto info = std::make_unique<ProofFamilyInfo>(context_);
                info->name = declaration.name;
                info->index_types = index_types;
                info->horn_complete = std::none_of(
                    declaration.constructors.begin(), declaration.constructors.end(),
                    [](syntax::ProofConstructor const &constructor) {
                        return !constructor.arbitrary_premises.empty();
                    });
                info->relation = context_.function(declaration.name.c_str(),
                                                   static_cast<unsigned>(index_sorts.size()), index_sorts.data(),
                                                   context_.bool_sort());
                if (info->horn_complete)
                    fixedpoint_.register_relation(info->relation);
                ProofFamilyInfo *stable = info.get();
                proof_families_.emplace(declaration.name, std::move(info));

                std::set<std::string> constructor_names;
                std::string family_scope = "proof-family:" + declaration.name;
                if (rainfall_)
                    rainfall_->record(
                        "object", "fine.proof-family.relation", {family_scope}, "fine.elaborator",
                        stable->horn_complete
                            ? "Erased indexed proposition represented by a native-sort least relation"
                            : "Erased indexed proposition represented for induction by a compiler-owned constructor table and relation-shaped term handle; no constructor is registered with fixedpoint because one has an arbitrary field",
                        {RainfallRecorder::string_field("family", declaration.name),
                         RainfallRecorder::string_field("relation", stable->relation.name().str()),
                         RainfallRecorder::number_field("indices", stable->index_types.size()),
                         RainfallRecorder::boolean_field("least_relation", stable->horn_complete),
                         RainfallRecorder::boolean_field("horn_complete", stable->horn_complete),
                         RainfallRecorder::boolean_field("proof_witnesses_erased", true)});

                for (syntax::ProofConstructor const &constructor : declaration.constructors) {
                    if (!constructor_names.insert(constructor.name).second)
                        reject(constructor.span, "duplicate proof constructor `" + constructor.name + "`");

                    ExpressionEnvironment environment;
                    z3::expr_vector formal_parameters(context_);
                    ProofConstructorInfo retained_constructor;
                    retained_constructor.name = constructor.name;
                    std::set<std::string> parameter_names;
                    for (std::size_t i = 0; i < constructor.parameters.size(); ++i) {
                        syntax::Parameter const &parameter = constructor.parameters[i];
                        if (!parameter_names.insert(parameter.name).second)
                            reject(parameter.span, "duplicate proof-constructor parameter `" + parameter.name + "`");
                        TypePtr type = resolve_type(parameter.type);
                        if (type->kind == RuntimeType::Kind::table)
                            reject(parameter.type.span,
                                   "proof-constructor parameters must be native Fine values, not Table");
                        std::string internal = "Fine.proof." + declaration.name + "." + constructor.name +
                                               ".arg" + std::to_string(i);
                        z3::expr term = context_.constant(internal.c_str(), type->sort);
                        environment.emplace(parameter.name, TypedExpression{type, term});
                        formal_parameters.push_back(term);
                        retained_constructor.parameters.push_back(term);
                    }

                    auto atom = [&](syntax::Expr const &expression, std::string_view role) {
                        if (expression.kind != syntax::Expr::Kind::call ||
                            !proof_families_.contains(expression.name))
                            reject(expression.span, std::string(role) +
                                                        " must be a direct call to a declared proof family");
                        TypedExpression elaborated = elaborate_expression(expression, environment);
                        if (!same(elaborated.type, bool_type_))
                            reject(expression.span, std::string(role) + " must be an indexed proposition");
                        return elaborated.expression;
                    };

                    z3::expr conclusion = atom(constructor.result, "proof-constructor result");
                    if (constructor.result.name != declaration.name)
                        reject(constructor.result.span, "constructor `" + constructor.name + "` must produce `" +
                                                            declaration.name + "(...)`");
                    for (syntax::Expr const &index : constructor.result.elements)
                        retained_constructor.result_indices.push_back(
                            elaborate_expression(index, environment).expression);

                    std::set<std::string> names_in_conclusion;
                    std::function<void(syntax::Expr const &)> collect_names = [&](syntax::Expr const &expression) {
                        if (expression.kind == syntax::Expr::Kind::name)
                            names_in_conclusion.insert(expression.name);
                        for (syntax::Expr const &child : expression.elements)
                            collect_names(child);
                    };
                    collect_names(constructor.result);
                    for (syntax::Parameter const &parameter : constructor.parameters) {
                        if (!names_in_conclusion.contains(parameter.name))
                            reject(parameter.span, "parameter `" + parameter.name +
                                                       "` does not occur in the constructor result; a premise-only "
                                                       "parameter would be one-witness search, not a universal proof field");
                    }

                    z3::expr premises = context_.bool_val(true);
                    std::vector<z3::expr> premise_terms;
                    std::size_t recursive_premises = 0;
                    retained_constructor.premise_count = constructor.premises.size();
                    for (syntax::Expr const &premise : constructor.premises) {
                        z3::expr elaborated = atom(premise, "proof-constructor premise");
                        premises = premises && elaborated;
                        premise_terms.push_back(elaborated);
                        if (premise.name == declaration.name) {
                            ++recursive_premises;
                            std::vector<z3::expr> indices;
                            for (syntax::Expr const &index : premise.elements)
                                indices.push_back(elaborate_expression(index, environment).expression);
                            retained_constructor.recursive_premise_indices.push_back(std::move(indices));
                        }
                    }

                    for (std::size_t field_ordinal = 0;
                         field_ordinal < constructor.arbitrary_premises.size(); ++field_ordinal) {
                        syntax::ArbitraryPremise const &field = constructor.arbitrary_premises[field_ordinal];
                        bool previous_capture = capture_source_edges_;
                        std::vector<std::string> previous_within = source_edge_within_;
                        capture_source_edges_ = true;
                        source_edge_within_ = {family_scope};
                        if (constructor.arbitrary_premises.size() != 1)
                            reject(field.span,
                                   "the first arbitrary-fresh slice admits exactly one such field per constructor");
                        if (parameter_names.contains(field.binder))
                            reject(field.span, "arbitrary-fresh name `" + field.binder +
                                                   "` shadows a constructor parameter");
                        auto found_view = views_.find(field.view_name);
                        if (found_view == views_.end())
                            reject(field.span, "unknown constrained view `" + field.view_name + "`");
                        ViewInfo const &view = found_view->second;
                        if (field.view_arguments.size() != view.parameter_types.size())
                            reject(field.span, "constrained view `" + field.view_name + "` expects " +
                                                   std::to_string(view.parameter_types.size()) + " arguments");

                        std::vector<TypedExpression> view_arguments;
                        for (std::size_t i = 0; i < field.view_arguments.size(); ++i) {
                            TypedExpression argument = elaborate_expression(field.view_arguments[i], environment);
                            if (!same(argument.type, view.parameter_types[i]))
                                reject(field.view_arguments[i].span,
                                       "constrained-view argument " + std::to_string(i + 1) + " must have type `" +
                                           view.parameter_types[i]->display + "`");
                            view_arguments.push_back(std::move(argument));
                        }

                        ProofConstructorInfo::ArbitraryField retained_field(context_);
                        retained_field.binder = field.binder;
                        retained_field.view_name = field.view_name;
                        retained_field.binder_term = context_.constant(
                            ("Fine.proof." + declaration.name + "." + constructor.name + ".arbitrary" +
                             std::to_string(field_ordinal)).c_str(),
                            view.carrier->sort);
                        if (rainfall_)
                            rainfall_->source_term(field.node_id, field.span, "proof.arbitrary-field",
                                                   retained_field.binder_term, "generated", {family_scope});

                        ExpressionEnvironment scoped_environment = environment;
                        scoped_environment.emplace(
                            field.binder, TypedExpression{view.carrier, retained_field.binder_term});
                        ExpressionEnvironment requirement_environment;
                        for (std::size_t i = 0; i < view.parameter_names.size(); ++i)
                            requirement_environment.emplace(view.parameter_names[i], view_arguments[i]);
                        requirement_environment.emplace(
                            "value", TypedExpression{view.carrier, retained_field.binder_term});

                        retained_field.requirement = context_.bool_val(true);
                        for (syntax::Expr const &requirement : view.requirements) {
                            TypedExpression elaborated = elaborate_expression(requirement, requirement_environment);
                            if (!same(elaborated.type, bool_type_))
                                throw std::runtime_error("validated view requirement changed type");
                            retained_field.requirement = retained_field.requirement && elaborated.expression;
                        }

                        for (syntax::Expr const &scoped_premise : field.premises) {
                            z3::expr elaborated = [&] {
                                if (scoped_premise.kind != syntax::Expr::Kind::call ||
                                    !proof_families_.contains(scoped_premise.name))
                                    reject(scoped_premise.span,
                                           "an arbitrary-fresh premise must call a declared proof family");
                                TypedExpression result = elaborate_expression(scoped_premise, scoped_environment);
                                if (!same(result.type, bool_type_))
                                    reject(scoped_premise.span,
                                           "an arbitrary-fresh premise must be an indexed proposition");
                                return result.expression;
                            }();
                            if (scoped_premise.name != declaration.name)
                                reject(scoped_premise.span,
                                       "the first arbitrary-fresh slice requires a recursive premise on `" +
                                           declaration.name + "`");
                            retained_field.premise_terms.push_back(elaborated);
                            std::vector<z3::expr> indices;
                            for (unsigned i = 0; i < elaborated.num_args(); ++i)
                                indices.push_back(elaborated.arg(i));
                            retained_field.recursive_premise_indices.push_back(std::move(indices));
                        }

                        if (rainfall_)
                            rainfall_->record(
                                "derive", "fine.proof-constructor.arbitrary-field", {family_scope},
                                "fine.elaborator",
                                "Compiler-owned arbitrary-fresh proof field; its view requirement and recursive premise are retained and are deliberately not inserted into a Horn body",
                                {RainfallRecorder::string_field("family", declaration.name),
                                 RainfallRecorder::string_field("constructor", constructor.name),
                                 RainfallRecorder::number_field("field_ordinal", field_ordinal),
                                 RainfallRecorder::string_field("binder", field.binder),
                                 RainfallRecorder::string_field("binder_term", rainfall_->term(retained_field.binder_term)),
                                 RainfallRecorder::string_field("view", field.view_name),
                                 RainfallRecorder::string_field("requirement", rainfall_->term(retained_field.requirement)),
                                 RainfallRecorder::number_field("recursive_premises", retained_field.premise_terms.size()),
                                 RainfallRecorder::boolean_field("lowered_to_horn", false)});
                        retained_constructor.arbitrary_fields.push_back(std::move(retained_field));
                        capture_source_edges_ = previous_capture;
                        source_edge_within_ = std::move(previous_within);
                    }

                    if (constructor.arbitrary_premises.empty()) {
                        z3::expr rule = constructor.premises.empty() ? conclusion : z3::implies(premises, conclusion);
                        if (!formal_parameters.empty())
                            rule = z3::forall(formal_parameters, rule);
                        if (stable->horn_complete) {
                            std::string rule_name = declaration.name + "." + constructor.name;
                            fixedpoint_.add_rule(rule, context_.str_symbol(rule_name.c_str()));
                        }

                        if (rainfall_) {
                            rainfall_->source_term(declaration.node_id, declaration.span, "decl.proof-family", rule,
                                                   "generated", {family_scope});
                            std::vector<RainfallField> data{
                                RainfallRecorder::string_field("family", declaration.name),
                                RainfallRecorder::string_field("constructor", constructor.name),
                                RainfallRecorder::string_field("conclusion", rainfall_->term(conclusion)),
                                RainfallRecorder::number_field("premises", premise_terms.size()),
                                RainfallRecorder::number_field("recursive_premises", recursive_premises),
                                RainfallRecorder::boolean_field("lowered_to_horn", stable->horn_complete),
                                RainfallRecorder::boolean_field("proof_witness_erased", true)};
                            data.push_back(stable->horn_complete
                                               ? RainfallRecorder::string_field("rule", rainfall_->term(rule))
                                               : RainfallRecorder::string_field("branch_schema", rainfall_->term(rule)));
                            rainfall_->record(
                                "derive", stable->horn_complete ? "fine.proof-constructor.rule"
                                                                : "fine.proof-constructor.branch",
                                {family_scope}, "fine.elaborator",
                                stable->horn_complete
                                    ? "Strictly-positive first-order constructor compiled to one least-relation Horn rule"
                                    : "First-order constructor retained for compiler-owned induction but not registered as a Horn rule because another constructor has an arbitrary field",
                                data);
                        }
                    }
                    stable->constructors.push_back(std::move(retained_constructor));
                }
            }

            syntax::Expr lift_expression(z3::expr const &expression,
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

            syntax::Expr lift_typed_expression(z3::expr const &expression, TypePtr const &type) const {
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

            static char const *operator_text(syntax::Expr::BinaryOp operation) {
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

            static void print_expression(std::ostream &output, syntax::Expr const &expression) {
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
                case syntax::Expr::Kind::hole:
                    output << '?' << expression.name;
                    return;
                }
            }

            int execute_match_synthesis(syntax::SynthDecl const &declaration) {
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
                    z3::expr term = context_.constant(
                        ("Fine.synth." + declaration.name + ".arg" + std::to_string(i)).c_str(), type->sort);
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
                    reject(declaration.scrutinee->span,
                           "a synthesis match requires a field-bearing datatype parameter");

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
                    rainfall_->record(
                        "scope", "synth.run.open", {"synth:" + declaration.name}, "fine.synthesis",
                        "Fine-owned exhaustive match skeleton with independently typed open arms",
                        {RainfallRecorder::string_field("name", declaration.name),
                         RainfallRecorder::string_field("matched_parameter", declaration.scrutinee->name),
                         RainfallRecorder::string_field("specification", rainfall_->term(specification)),
                         RainfallRecorder::number_field("arms", declaration.arms.size())});
                }

                for (syntax::MatchArm const &arm : declaration.arms) {
                    auto found_case = case_indices.find(arm.constructor);
                    if (found_case == case_indices.end())
                        reject(arm.span, "constructor `" + arm.constructor + "` does not belong to `" +
                                             datatype->name + "`");
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
                            ("Fine.synth." + declaration.name + ".arm." + arm.constructor + ".field" +
                             std::to_string(i)).c_str(),
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
                    z3::expr constructed = item.constructor(
                        static_cast<unsigned>(field_symbols.size()), field_symbols.data());
                    arm_environment.insert_or_assign(declaration.scrutinee->name,
                                                     TypedExpression{matched_type, constructed});
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
                            std::string source_id = rainfall_->source_node(
                                arm.value.node_id, arm.value.span, "expr.hole");
                            std::vector<std::string> grammar_refs;
                            for (z3::expr const &input : grammar_inputs)
                                grammar_refs.push_back(rainfall_->term(input));
                            rainfall_->record(
                                "object", "synth.hole.declare", {"synth:" + declaration.name},
                                "fine.synthesis",
                                "Snapshot-scoped typed source hole and its fixed integer grammar inputs",
                                {RainfallRecorder::string_field(
                                     "id", "hole:" + std::to_string(arm.value.node_id)),
                                 RainfallRecorder::string_field("snapshot", "snapshot:0"),
                                 RainfallRecorder::string_field("source", source_id),
                                 RainfallRecorder::string_field("name", arm.value.name),
                                 RainfallRecorder::string_field("expected_type", "Int"),
                                 RainfallRecorder::string_field("grammar", "fine.qf-lia-int.v1"),
                                 RainfallRecorder::raw_field(
                                     "grammar_inputs", RainfallRecorder::string_array(grammar_refs))});
                        }
                        RefutationSynthesizer synthesizer(
                            context_, arm_name, arm_parameters, result, arm_specification, rainfall_.get(),
                            grammar_inputs, true);
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
                                "transition", "synth.arm.close", {"synth-arm:" + arm_name},
                                "fine.synthesis", "One open match arm produced an independently verified witness",
                                {RainfallRecorder::string_field("hole", "hole:" +
                                                                         std::to_string(arm.value.node_id)),
                                 RainfallRecorder::string_field("constructor", arm.constructor),
                                 RainfallRecorder::string_field("body", rendered.str()),
                                 RainfallRecorder::string_field("semantic_term", rainfall_->term(arm_value)),
                                 RainfallRecorder::string_field("status", "verified")});
                        }
                        if (replacements_json.size() > 1)
                            replacements_json += ',';
                        replacements_json += "{\"hole\":" +
                                             RainfallRecorder::quote("hole:" +
                                                                       std::to_string(arm.value.node_id)) +
                                             ",\"from\":" + std::to_string(arm.value.span.begin.offset) +
                                             ",\"to\":" + std::to_string(arm.value.span.end.offset) +
                                             ",\"insert\":" + RainfallRecorder::quote(rendered.str()) + '}';
                    }
                    else {
                        std::function<void(syntax::Expr const &)> reject_nested_hole =
                            [&](syntax::Expr const &expression) {
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
                    rainfall_->record(
                        "constraint", "synth.match.counterexample.assert", {run_scope}, "fine.synthesis",
                        "Negation of the completed exhaustive match specification",
                        {RainfallRecorder::string_field("body", rainfall_->term(body)),
                         RainfallRecorder::string_field("specification", rainfall_->term(verified_specification)),
                         RainfallRecorder::string_field("assertion", rainfall_->term(counterexample_query))});
                    rainfall_->record(
                        "scope", "solver.query.open", {run_scope, verification_query}, "fine.synthesis",
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
                    char const *status = verification == z3::sat ? "sat"
                                               : verification == z3::unsat ? "unsat"
                                                                           : "unknown";
                    rainfall_->record(
                        "transition", "solver.query.result", {run_scope, verification_query}, "z3.public-api",
                        "Final public result for the whole-match verification query",
                        {RainfallRecorder::string_field("query", verification_query),
                         RainfallRecorder::string_field("status", status),
                         RainfallRecorder::string_field("polarity", "counterexample-exists"),
                         RainfallRecorder::string_field("domain_outcome", verification == z3::sat ? "refuted"
                                                                              : verification == z3::unsat
                                                                                    ? "verified"
                                                                                    : "unknown")});
                    rainfall_->record(
                        "scope", "solver.query.close", {run_scope, verification_query}, "fine.synthesis",
                        "Whole-match public solver query lifetime",
                        {RainfallRecorder::string_field("id", verification_query)});
                }
                if (verification == z3::unknown)
                    reject(declaration.span, "materialized match verification was unknown: " +
                                                 verifier.reason_unknown());
                if (verification != z3::unsat)
                    reject(declaration.span, "materialized match does not satisfy its specification");

                if (rainfall_) {
                    rainfall_->source_term(declaration.node_id, declaration.span, "decl.synth", body,
                                           "generated", {"synth:" + declaration.name});
                    rainfall_->record(
                        "object", "fine.match-witness", {"synth:" + declaration.name}, "fine.runtime",
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
                        {RainfallRecorder::string_field(
                             "status", open_arms ? "source-program" : "verified-materialized"),
                         RainfallRecorder::number_field("open_arms", open_arms)});
                }

                output_ << (open_arms ? "source-match: synthesized " : "verified-match: ") << declaration.name
                        << " with " << open_arms << " open arms; selected " << selections
                        << " ground instances; cores kept " << core_members << '\n';
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

            int execute_synthesis(syntax::SynthDecl const &declaration) {
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
                if (!same(roundtrip.type, result_type) ||
                    !Z3_is_eq_ast(context_, roundtrip.expression, synthesized.witness))
                    reject(declaration.span, "parse(print(lift(witness))) violated exact AST identity");

                if (rainfall_) {
                    rainfall_->record(
                        "object", "fine.source-witness", {"synth:" + declaration.name}, "fine.runtime",
                        "Lifted, printed, parsed, elaborated witness with exact same-manager AST identity",
                        {RainfallRecorder::string_field("declaration", declaration.name),
                         RainfallRecorder::string_field("body", body),
                         RainfallRecorder::string_field("semantic_term", rainfall_->term(synthesized.witness)),
                         RainfallRecorder::boolean_field("parse_reify_exact_identity", true)});
                    rainfall_->record("transition", "fine.witness.accept", {"synth:" + declaration.name},
                                      "fine.runtime", "Backend verification plus Fine source round-trip identity check",
                                       {RainfallRecorder::string_field("declaration", declaration.name),
                                        RainfallRecorder::string_field("status", "source-program"),
                                        RainfallRecorder::boolean_field("verified", true)});
                    rainfall_->validate_terms();
                    rainfall_->record("scope", "synth.run.close", {"synth:" + declaration.name}, "fine.runtime",
                                      "Native synthesis plus Fine source witness round trip",
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

            int execute_proof_family_check(syntax::CheckDecl const &declaration) {
                reserve_value_name(declaration.name, declaration.span);
                if (declaration.induction_parameter)
                    reject(*declaration.induction_span,
                           "proof-family membership does not yet implement derivation induction");
                if (declaration.proof_induction)
                    return execute_proof_family_induction(declaration);
                if (!declaration.parameters.empty())
                    return execute_proof_family_invariant(declaration);
                if (!declaration.assumes.empty())
                    reject(declaration.span, "a least-relation membership check cannot yet mix ordinary assumptions");
                if (declaration.ensures.size() != 1)
                    reject(declaration.span, "a least-relation membership check needs exactly one ensured atom");
                syntax::Expr const &source_query = declaration.ensures.front();
                if (source_query.kind != syntax::Expr::Kind::call ||
                    !proof_families_.contains(source_query.name))
                    reject(source_query.span, "the ensured condition must be a direct proof-family call");
                if (!proof_families_.at(source_query.name)->horn_complete)
                    reject(source_query.span,
                           "least-relation membership is unavailable because `" + source_query.name +
                               "` has an arbitrary-fresh constructor retained outside Horn lowering");

                std::string run_scope = "proof-check:" + declaration.name;
                capture_source_edges_ = true;
                source_edge_within_ = {run_scope};
                ExpressionEnvironment environment;
                TypedExpression elaborated = elaborate_expression(source_query, environment);
                capture_source_edges_ = false;
                source_edge_within_.clear();
                z3::expr query = elaborated.expression;

                std::unique_ptr<RainfallFixedpointObserver> fixedpoint_observer;
                if (rainfall_) {
                    rainfall_->record(
                        "scope", "proof-check.run.open", {run_scope}, "fine.fixedpoint",
                        "One public least-relation membership query; records admitted constructor rules, public Spacer callback boundaries, and the public result",
                        {RainfallRecorder::string_field("declaration", declaration.name),
                         RainfallRecorder::string_field("family", source_query.name),
                         RainfallRecorder::string_field("query", rainfall_->term(query)),
                         RainfallRecorder::boolean_field("ground", true)});
                    z3::params parameters(context_);
                    parameters.set("engine", "spacer");
                    parameters.set("spacer.p3.share_invariants", true);
                    parameters.set("spacer.p3.share_lemmas", true);
                    fixedpoint_.set(parameters);
                    fixedpoint_observer = std::make_unique<RainfallFixedpointObserver>(
                        fixedpoint_, *rainfall_, std::vector<std::string>{run_scope});
                }

                z3::check_result result = fixedpoint_.query(query);
                if (fixedpoint_observer)
                    fixedpoint_observer->rethrow_if_failed();
                if (result == z3::unknown)
                    reject(source_query.span,
                           "least-relation membership was unknown: " + fixedpoint_.reason_unknown());
                bool derived = result == z3::sat;
                if (rainfall_) {
                    z3::expr answer = fixedpoint_.get_answer();
                    rainfall_->record(
                        "transition", "solver.fixedpoint.result", {run_scope}, "z3.public-api",
                        "Final public fixedpoint result only; no claim about internal rule-search steps",
                        {RainfallRecorder::string_field("query", rainfall_->term(query)),
                         RainfallRecorder::string_field("answer", rainfall_->term(answer)),
                         RainfallRecorder::string_field("status", derived ? "sat" : "unsat"),
                         RainfallRecorder::string_field("domain_outcome", derived ? "derived" : "not-derived")});
                    rainfall_->validate_terms();
                    rainfall_->record("scope", "proof-check.run.close", {run_scope}, "fine.fixedpoint",
                                      "Ground least-relation membership completed",
                                      {RainfallRecorder::string_field("status", derived ? "derived" : "not-derived")});
                }
                output_ << (derived ? "derived: " : "not-derived: ") << declaration.name << '\n';
                output_ << "proof-family: " << source_query.name << '\n';
                output_ << "proof-witness: erased\n";
                return 0;
            }

            int execute_proof_family_induction(syntax::CheckDecl const &declaration) {
                syntax::Expr const &target = *declaration.proof_induction;
                if (!proof_families_.contains(target.name))
                    reject(target.span, "`inducts` target must call a declared proof family");
                if (declaration.assumes.size() != 1)
                    reject(declaration.span,
                           "proof-family induction needs exactly its target atom in `assumes`");
                syntax::Expr const &assumed = declaration.assumes.front();
                if (assumed.kind != syntax::Expr::Kind::call || assumed.name != target.name)
                    reject(assumed.span,
                           "proof-family induction assumption must be the same family as `inducts`");

                ProofFamilyInfo const &family = *proof_families_.at(target.name);
                if (declaration.parameters.size() != family.index_types.size() ||
                    target.elements.size() != declaration.parameters.size())
                    reject(target.span,
                           "the first proof-induction slice needs exactly one check parameter per family index");

                ExpressionEnvironment environment;
                z3::expr_vector check_terms(context_);
                std::set<std::string> parameter_names;
                for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                    syntax::Parameter const &parameter = declaration.parameters[i];
                    if (!parameter_names.insert(parameter.name).second)
                        reject(parameter.span, "duplicate check parameter `" + parameter.name + "`");
                    TypePtr type = resolve_type(parameter.type);
                    if (!same(type, family.index_types[i]))
                        reject(parameter.type.span, "proof-induction parameter " + std::to_string(i + 1) +
                                                        " must have type `" + family.index_types[i]->display + "`");
                    if (target.elements[i].kind != syntax::Expr::Kind::name ||
                        target.elements[i].name != parameter.name)
                        reject(target.elements[i].span,
                               "proof-induction indices must be the check parameters in declaration order");
                    std::string internal = "Fine.proof-induction." + declaration.name + ".arg" +
                                           std::to_string(i);
                    z3::expr term = context_.constant(internal.c_str(), type->sort);
                    environment.emplace(parameter.name, TypedExpression{type, term});
                    check_terms.push_back(term);
                }

                std::function<void(syntax::Expr const &)> reject_family_in_guarantee =
                    [&](syntax::Expr const &expression) {
                        if (expression.kind == syntax::Expr::Kind::call &&
                            proof_families_.contains(expression.name))
                            reject(expression.span,
                                   "proof-family atoms are not yet admitted inside induction guarantees");
                        for (syntax::Expr const &child : expression.elements)
                            reject_family_in_guarantee(child);
                    };
                for (syntax::Expr const &condition : declaration.ensures)
                    reject_family_in_guarantee(condition);

                std::string run_scope = "proof-induction:" + declaration.name;
                capture_source_edges_ = true;
                source_edge_within_ = {run_scope};
                TypedExpression target_term = elaborate_expression(target, environment);
                TypedExpression assumed_term = elaborate_expression(assumed, environment);
                if (!Z3_is_eq_ast(context_, target_term.expression, assumed_term.expression))
                    reject(assumed.span, "`inducts` target and assumed family atom must elaborate identically");
                z3::expr guarantees = context_.bool_val(true);
                for (syntax::Expr const &condition : declaration.ensures) {
                    TypedExpression elaborated = elaborate_expression(condition, environment);
                    if (!same(elaborated.type, bool_type_))
                        reject(condition.span, "proof-induction guarantee must have type Bool");
                    guarantees = guarantees && elaborated.expression;
                }
                capture_source_edges_ = false;
                source_edge_within_.clear();

                if (rainfall_)
                    rainfall_->record(
                        "scope", "proof-induction.run.open", {run_scope}, "fine.induction",
                        "Fine-owned structural induction over proof-family constructors; each recursive family premise becomes one exact induction hypothesis and each branch is checked by a separate public SMT query",
                        {RainfallRecorder::string_field("declaration", declaration.name),
                         RainfallRecorder::string_field("family", family.name),
                         RainfallRecorder::number_field("constructors", family.constructors.size()),
                         RainfallRecorder::string_field("target", rainfall_->term(target_term.expression)),
                         RainfallRecorder::string_field("guarantees", rainfall_->term(guarantees))});

                auto instantiate = [&](z3::expr const &expression,
                                       std::vector<z3::expr> const &indices) {
                    if (indices.size() != check_terms.size())
                        throw std::runtime_error("internal proof-family index arity mismatch");
                    z3::expr_vector replacements(context_);
                    for (z3::expr const &index : indices)
                        replacements.push_back(index);
                    z3::expr result = expression;
                    return result.substitute(check_terms, replacements);
                };

                std::string failed_branch;
                for (ProofConstructorInfo const &constructor : family.constructors) {
                    if (constructor.premise_count != constructor.recursive_premise_indices.size())
                        reject(target.span,
                               "proof induction currently requires every constructor premise to recurse on `" +
                                   family.name + "`");
                    std::string branch_scope = "branch:" + constructor.name;
                    z3::expr constructor_result = family.relation(
                        static_cast<unsigned>(constructor.result_indices.size()),
                        constructor.result_indices.data());
                    z3::expr branch_goal = instantiate(guarantees, constructor.result_indices);
                    z3::expr hypotheses = context_.bool_val(true);
                    std::vector<z3::expr> hypothesis_terms;
                    for (std::size_t i = 0; i < constructor.recursive_premise_indices.size(); ++i) {
                        std::vector<z3::expr> const &indices = constructor.recursive_premise_indices[i];
                        z3::expr hypothesis = instantiate(guarantees, indices);
                        hypotheses = hypotheses && hypothesis;
                        hypothesis_terms.push_back(hypothesis);
                        if (rainfall_) {
                            z3::expr premise = family.relation(static_cast<unsigned>(indices.size()), indices.data());
                            rainfall_->record(
                                "derive", "proof-induction.hypothesis", {run_scope, branch_scope},
                                "fine.induction",
                                "Compiler-owned recursive constructor premise paired with the guarantee instantiated at its exact indices",
                                {RainfallRecorder::string_field("constructor", constructor.name),
                                 RainfallRecorder::number_field("premise_ordinal", i),
                                 RainfallRecorder::string_field("recursive_premise", rainfall_->term(premise)),
                                 RainfallRecorder::string_field("induction_hypothesis", rainfall_->term(hypothesis))});
                        }
                    }

                    for (std::size_t field_ordinal = 0;
                         field_ordinal < constructor.arbitrary_fields.size(); ++field_ordinal) {
                        ProofConstructorInfo::ArbitraryField const &field =
                            constructor.arbitrary_fields[field_ordinal];

                        z3::expr_vector binders(context_);
                        binders.push_back(field.binder_term);
                        z3::expr availability = z3::exists(binders, field.requirement);
                        if (!constructor.parameters.empty()) {
                            z3::expr_vector constructor_parameters(context_);
                            for (z3::expr const &parameter : constructor.parameters)
                                constructor_parameters.push_back(parameter);
                            availability = z3::forall(constructor_parameters, availability);
                        }
                        z3::solver availability_solver(context_);
                        availability_solver.add(!availability);
                        z3::check_result availability_result = availability_solver.check();
                        if (availability_result == z3::unknown)
                            reject(target.span,
                                   "arbitrary-fresh availability for constructor `" + constructor.name +
                                       "` was unknown: " + availability_solver.reason_unknown());
                        bool available = availability_result == z3::unsat;
                        if (rainfall_)
                            rainfall_->record(
                                "transition", "proof-induction.arbitrary.availability",
                                {run_scope, branch_scope}, "z3.public-api",
                                "Fine separately checks that every constructor-parameter assignment admits a carrier value satisfying the constrained view; this prevents vacuous arbitrary-fresh branches",
                                {RainfallRecorder::string_field("constructor", constructor.name),
                                 RainfallRecorder::number_field("field_ordinal", field_ordinal),
                                 RainfallRecorder::string_field("binder", field.binder),
                                 RainfallRecorder::string_field("view", field.view_name),
                                 RainfallRecorder::string_field("requirement", rainfall_->term(field.requirement)),
                                 RainfallRecorder::string_field("obligation", rainfall_->term(availability)),
                                 RainfallRecorder::string_field("status", available ? "unsat" : "sat"),
                                 RainfallRecorder::string_field("domain_outcome", available ? "available" : "empty")});
                        if (!available)
                            reject(target.span,
                                   "constrained view `" + field.view_name + "` is empty for some parameters of `" +
                                       constructor.name + "`; arbitrary-fresh induction would be vacuous");

                        hypotheses = hypotheses && field.requirement;
                        for (std::size_t i = 0; i < field.recursive_premise_indices.size(); ++i) {
                            std::vector<z3::expr> const &indices = field.recursive_premise_indices[i];
                            z3::expr hypothesis = instantiate(guarantees, indices);
                            hypotheses = hypotheses && hypothesis;
                            hypothesis_terms.push_back(hypothesis);
                            if (rainfall_)
                                rainfall_->record(
                                    "derive", "proof-induction.arbitrary-hypothesis",
                                    {run_scope, branch_scope}, "fine.induction",
                                    "Exact recursive premise and induction hypothesis under one scoped arbitrary-fresh carrier value and its independently retained view requirement",
                                    {RainfallRecorder::string_field("constructor", constructor.name),
                                     RainfallRecorder::number_field("field_ordinal", field_ordinal),
                                     RainfallRecorder::number_field("premise_ordinal", i),
                                     RainfallRecorder::string_field("binder", field.binder),
                                     RainfallRecorder::string_field("binder_term", rainfall_->term(field.binder_term)),
                                     RainfallRecorder::string_field("view", field.view_name),
                                     RainfallRecorder::string_field("requirement", rainfall_->term(field.requirement)),
                                     RainfallRecorder::string_field("recursive_premise", rainfall_->term(field.premise_terms[i])),
                                     RainfallRecorder::string_field("induction_hypothesis", rainfall_->term(hypothesis)),
                                     RainfallRecorder::string_field("scope_owner", "fine-arbitrary-field")});
                        }
                    }
                    z3::expr branch_query = hypotheses && !branch_goal;
                    if (rainfall_) {
                        rainfall_->source_term(declaration.node_id, declaration.span, "decl.check", branch_query,
                                               "generated", {run_scope, branch_scope});
                        rainfall_->record(
                            "scope", "proof-induction.branch.open", {run_scope, branch_scope}, "fine.induction",
                            "One constructor branch with compiler-owned result indices and recursive hypotheses",
                            {RainfallRecorder::string_field("constructor", constructor.name),
                             RainfallRecorder::number_field("constructor_parameters", constructor.parameters.size()),
                             RainfallRecorder::string_field("constructor_result", rainfall_->term(constructor_result)),
                             RainfallRecorder::string_field("goal", rainfall_->term(branch_goal)),
                             RainfallRecorder::string_field("counterexample_query", rainfall_->term(branch_query)),
                             RainfallRecorder::number_field("recursive_hypotheses", hypothesis_terms.size()),
                             RainfallRecorder::number_field("arbitrary_fields", constructor.arbitrary_fields.size())});
                    }

                    z3::solver solver(context_);
                    solver.add(branch_query);
                    z3::check_result result = solver.check();
                    if (result == z3::unknown)
                        reject(target.span, "proof-induction branch `" + constructor.name +
                                                "` was unknown: " + solver.reason_unknown());
                    bool branch_verified = result == z3::unsat;
                    if (rainfall_) {
                        rainfall_->record(
                            "transition", "proof-induction.branch.result", {run_scope, branch_scope},
                            "z3.public-api",
                            "Final public SMT result for one compiler-generated constructor branch",
                            {RainfallRecorder::string_field("constructor", constructor.name),
                             RainfallRecorder::string_field("status", branch_verified ? "unsat" : "sat"),
                             RainfallRecorder::string_field("domain_outcome", branch_verified ? "verified" : "refuted")});
                        rainfall_->record("scope", "proof-induction.branch.close", {run_scope, branch_scope},
                                          "fine.induction", "Constructor induction branch completed",
                                          {RainfallRecorder::string_field("status", branch_verified ? "verified" : "refuted")});
                    }
                    if (!branch_verified) {
                        failed_branch = constructor.name;
                        break;
                    }
                }

                bool verified = failed_branch.empty();
                if (rainfall_) {
                    rainfall_->validate_terms();
                    rainfall_->record(
                        "scope", "proof-induction.run.close", {run_scope}, "fine.induction",
                        "Compiler-generated proof-family induction completed",
                        {RainfallRecorder::string_field("status", verified ? "verified" : "refuted"),
                         RainfallRecorder::string_field("failed_constructor", failed_branch)});
                }
                output_ << (verified ? "verified-proof-induction: " : "refuted-proof-induction: ")
                        << declaration.name << '\n';
                output_ << "proof-family: " << family.name << '\n';
                if (verified)
                    output_ << "constructor-branches: " << family.constructors.size() << " verified\n";
                else
                    output_ << "failed-constructor: " << failed_branch << '\n';
                output_ << "proof-witness: erased after compiler-owned branch construction\n";
                return 0;
            }

            int execute_proof_family_invariant(syntax::CheckDecl const &declaration) {
                if (declaration.assumes.size() != 1)
                    reject(declaration.span,
                           "a proof-family invariant needs exactly one assumed family atom");
                syntax::Expr const &source_membership = declaration.assumes.front();
                if (source_membership.kind != syntax::Expr::Kind::call ||
                    !proof_families_.contains(source_membership.name))
                    reject(source_membership.span,
                           "the invariant assumption must be a direct proof-family call");
                if (!proof_families_.at(source_membership.name)->horn_complete)
                    reject(source_membership.span,
                           "fixedpoint invariant checking is unavailable because `" + source_membership.name +
                               "` has an arbitrary-fresh constructor retained outside Horn lowering");

                std::function<void(syntax::Expr const &)> reject_family_in_guarantee =
                    [&](syntax::Expr const &expression) {
                        if (expression.kind == syntax::Expr::Kind::call &&
                            proof_families_.contains(expression.name))
                            reject(expression.span,
                                   "proof-family atoms are not yet admitted inside invariant guarantees");
                        for (syntax::Expr const &child : expression.elements)
                            reject_family_in_guarantee(child);
                    };
                for (syntax::Expr const &condition : declaration.ensures)
                    reject_family_in_guarantee(condition);

                ExpressionEnvironment environment;
                z3::expr_vector formal_parameters(context_);
                std::set<std::string> parameter_names;
                for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                    syntax::Parameter const &parameter = declaration.parameters[i];
                    if (!parameter_names.insert(parameter.name).second)
                        reject(parameter.span, "duplicate check parameter `" + parameter.name + "`");
                    TypePtr type = resolve_type(parameter.type);
                    if (type->kind == RuntimeType::Kind::table)
                        reject(parameter.type.span,
                               "proof-family invariant parameters must be native Fine values, not Table");
                    std::string internal = "Fine.proof-check." + declaration.name + ".arg" +
                                           std::to_string(i);
                    z3::expr term = context_.constant(internal.c_str(), type->sort);
                    environment.emplace(parameter.name, TypedExpression{type, term});
                    formal_parameters.push_back(term);
                }

                std::string run_scope = "proof-check:" + declaration.name;
                capture_source_edges_ = true;
                source_edge_within_ = {run_scope};
                TypedExpression membership = elaborate_expression(source_membership, environment);
                z3::expr guarantees = context_.bool_val(true);
                for (syntax::Expr const &condition : declaration.ensures) {
                    TypedExpression elaborated = elaborate_expression(condition, environment);
                    if (!same(elaborated.type, bool_type_))
                        reject(condition.span, "proof-family invariant guarantee must have type Bool");
                    guarantees = guarantees && elaborated.expression;
                }
                capture_source_edges_ = false;
                source_edge_within_.clear();

                std::string counterexample_name = "Fine.proof-check." + declaration.name + ".counterexample";
                z3::func_decl counterexample = context_.function(
                    counterexample_name.c_str(), 0, nullptr, context_.bool_sort());
                fixedpoint_.register_relation(counterexample);
                z3::expr query = counterexample();
                z3::expr counterexample_body = membership.expression && !guarantees;
                z3::expr counterexample_rule = z3::implies(counterexample_body, query);
                counterexample_rule = z3::forall(formal_parameters, counterexample_rule);
                std::string rule_name = declaration.name + ".counterexample";
                fixedpoint_.add_rule(counterexample_rule, context_.str_symbol(rule_name.c_str()));

                std::unique_ptr<RainfallFixedpointObserver> fixedpoint_observer;
                if (rainfall_) {
                    rainfall_->source_term(declaration.node_id, declaration.span, "decl.check",
                                           counterexample_rule, "generated", {run_scope});
                    rainfall_->record(
                        "scope", "proof-check.run.open", {run_scope}, "fine.fixedpoint",
                        "One counterexample-reachability query over a least proof-family relation; exposes compiler translation, public Spacer callbacks, and the public answer, not rule matches or a source proof witness",
                        {RainfallRecorder::string_field("declaration", declaration.name),
                         RainfallRecorder::string_field("family", source_membership.name),
                         RainfallRecorder::number_field("parameters", declaration.parameters.size()),
                         RainfallRecorder::boolean_field("ground", false)});
                    rainfall_->record(
                        "transform", "proof-check.invariant.translate", {run_scope}, "fine.elaborator",
                        "Compiler-owned negated-guarantee reachability rule over the least family relation",
                        {RainfallRecorder::string_field("membership", rainfall_->term(membership.expression)),
                         RainfallRecorder::string_field("guarantees", rainfall_->term(guarantees)),
                         RainfallRecorder::string_field("counterexample_rule", rainfall_->term(counterexample_rule)),
                         RainfallRecorder::string_field("query", rainfall_->term(query)),
                         RainfallRecorder::string_field("polarity", "counterexample-reachable")});
                    z3::params parameters(context_);
                    parameters.set("engine", "spacer");
                    parameters.set("spacer.p3.share_invariants", true);
                    parameters.set("spacer.p3.share_lemmas", true);
                    fixedpoint_.set(parameters);
                    fixedpoint_observer = std::make_unique<RainfallFixedpointObserver>(
                        fixedpoint_, *rainfall_, std::vector<std::string>{run_scope});
                }

                z3::check_result result = fixedpoint_.query(query);
                if (fixedpoint_observer)
                    fixedpoint_observer->rethrow_if_failed();
                if (result == z3::unknown)
                    reject(declaration.span,
                           "proof-family invariant query was unknown: " + fixedpoint_.reason_unknown());
                bool verified = result == z3::unsat;
                z3::expr answer = fixedpoint_.get_answer();
                if (rainfall_) {
                    rainfall_->record(
                        "transition", "solver.fixedpoint.result", {run_scope}, "z3.public-api",
                        "Final public fixedpoint result and answer; retained callback lemmas are not claimed to cause this result",
                        {RainfallRecorder::string_field("query", rainfall_->term(query)),
                         RainfallRecorder::string_field("answer", rainfall_->term(answer)),
                         RainfallRecorder::string_field("status", verified ? "unsat" : "sat"),
                         RainfallRecorder::string_field("polarity", "counterexample-reachable"),
                         RainfallRecorder::string_field("domain_outcome", verified ? "verified" : "refuted")});
                    rainfall_->validate_terms();
                    rainfall_->record("scope", "proof-check.run.close", {run_scope}, "fine.fixedpoint",
                                      "Open proof-family invariant check completed",
                                      {RainfallRecorder::string_field("status", verified ? "verified" : "refuted")});
                }

                output_ << (verified ? "verified-family-invariant: " : "refuted-family-invariant: ")
                        << declaration.name << '\n';
                output_ << "proof-family: " << source_membership.name << '\n';
                output_ << (verified ? "counterexample: none\n"
                                     : "counterexample: fixedpoint reachability only (answer retained by rainfall)\n");
                return 0;
            }

            int execute_check(syntax::CheckDecl const &declaration) {
                if (!proof_families_.empty())
                    return execute_proof_family_check(declaration);
                if (declaration.proof_induction)
                    reject(declaration.proof_induction->span,
                           "proof-family induction target has no declared proof family");
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
                    if (!same(type, int_type_) && !same(type, bool_type_) &&
                        type->kind != RuntimeType::Kind::enumeration && type->kind != RuntimeType::Kind::datatype &&
                        type->kind != RuntimeType::Kind::tuple)
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
                    auto found = std::find_if(parameters.begin(), parameters.end(), [&](CheckParameter const &item) {
                        return item.name == induction_parameter;
                    });
                    if (found == parameters.end())
                        reject(*declaration.induction_span,
                               "unknown induction parameter `" + induction_parameter + "`");
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
                            direct_subterm = direct_subterm ||
                                             (is_case && smaller == item.accessors[i](found->term));
                            ++recursive_positions;
                        }
                    }
                    if (recursive_positions == 0)
                        reject(*declaration.induction_span,
                               "the induction datatype has no direct recursive fields");

                    z3::expr_vector source(context_);
                    z3::expr_vector destination(context_);
                    source.push_back(found->term);
                    destination.push_back(smaller);
                    z3::expr smaller_theorem = theorem.substitute(source, destination);
                    z3::expr hypothesis_body = z3::implies(direct_subterm, smaller_theorem);
                    std::vector<Z3_app> bound{
                        reinterpret_cast<Z3_app>(static_cast<Z3_ast>(smaller))};
                    std::string qid = "fine.induction." + declaration.name + "." + induction_parameter;
                    Z3_ast quantified = Z3_mk_quantifier_const_ex(
                        context_, true, 0, context_.str_symbol(qid.c_str()), context_.str_symbol(""),
                        static_cast<unsigned>(bound.size()), bound.data(), 0, nullptr, 0, nullptr,
                        hypothesis_body);
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
                         RainfallRecorder::string_field("induction_hypothesis",
                                                        rainfall_->term(induction_hypothesis)),
                         RainfallRecorder::string_field("assertion", rainfall_->term(counterexample_query))});
                    rainfall_->record(
                        "scope", "solver.query.open", {run_scope, query}, "fine.check",
                        declaration.induction_parameter
                            ? "Public solver assertion boundary with scoped read-only E-matching binding and "
                              "on-clause observers"
                            : "Public solver assertion boundary",
                        {RainfallRecorder::string_field("id", query),
                         RainfallRecorder::string_field(
                             "purpose", declaration.induction_parameter
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
                    clause_observer = std::make_unique<RainfallClauseObserver>(
                        solver, *rainfall_, std::vector<std::string>{run_scope, query});
                }
                z3::check_result result = solver.check();
                quantifier_observer.reset();
                clause_observer.reset();
                if (rainfall_) {
                    char const *status = result == z3::sat ? "sat" : result == z3::unsat ? "unsat" : "unknown";
                    rainfall_->record(
                        "transition", "solver.query.result", {run_scope, query}, "z3.public-api",
                        "Final public check result only; no claim about solver search or internal cause",
                        {RainfallRecorder::string_field("query", query),
                         RainfallRecorder::string_field("status", status),
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

            static void expect_table(Binding const &binding, syntax::SourceSpan span, std::string const &role) {
                if (binding.type->kind != RuntimeType::Kind::table)
                    reject(span, role + " must have Table type");
            }

            static void expect_bool_range(Binding const &binding, syntax::SourceSpan span, std::string const &role) {
                expect_table(binding, span, role);
                if (binding.type->arguments[1]->kind != RuntimeType::Kind::boolean)
                    reject(span, role + " must return Bool");
            }

            static std::map<std::string, syntax::Expr const *> take_map(syntax::ProofDecl const &proof) {
                std::map<std::string, syntax::Expr const *> result;
                for (syntax::NamedArgument const &argument : proof.takes) {
                    if (!result.emplace(argument.name, &argument.value).second)
                        reject(argument.span, "duplicate proof input `" + argument.name + "`");
                }
                static std::set<std::string> const expected{"relation",   "left_step",   "right_step",
                                                            "left_label", "right_label", "initial"};
                for (auto const &[name, expression] : result) {
                    (void)expression;
                    if (!expected.contains(name))
                        reject(proof.span, "unexpected proof input `" + name + "`");
                }
                for (std::string const &name : expected) {
                    if (!result.contains(name))
                        reject(proof.span, "missing proof input `" + name + "`");
                }
                return result;
            }

            int execute_bisimulation(syntax::ProofDecl const &proof) {
                if (proof.name != "bisimulation")
                    reject(proof.span,
                           "unknown proof form `" + proof.name + "`; this slice admits `proof bisimulation`");
                auto inputs = take_map(proof);
                Binding const &relation = binding(*inputs.at("relation"), "relation");
                Binding const &left_step = binding(*inputs.at("left_step"), "left_step");
                Binding const &right_step = binding(*inputs.at("right_step"), "right_step");
                Binding const &left_label = binding(*inputs.at("left_label"), "left_label");
                Binding const &right_label = binding(*inputs.at("right_label"), "right_label");

                expect_bool_range(relation, proof.span, "relation");
                if (!relation.is_model)
                    reject(inputs.at("relation")->span, "relation must name the `model` hole");
                TypePtr relation_domain = relation.type->arguments[0];
                if (relation_domain->kind != RuntimeType::Kind::tuple)
                    reject(inputs.at("relation")->span, "relation must be indexed by a pair of enum states");
                TypePtr left_type = relation_domain->arguments[0];
                TypePtr right_type = relation_domain->arguments[1];
                if (left_type->kind != RuntimeType::Kind::enumeration ||
                    right_type->kind != RuntimeType::Kind::enumeration)
                    reject(inputs.at("relation")->span, "bisimulation state types must be finite enums");

                validate_step(left_step, left_type, inputs.at("left_step")->span, "left_step");
                validate_step(right_step, right_type, inputs.at("right_step")->span, "right_step");
                validate_label(left_label, left_type, inputs.at("left_label")->span, "left_label");
                validate_label(right_label, right_type, inputs.at("right_label")->span, "right_label");

                if (proof.gives.kind != syntax::Expr::Kind::name || proof.gives.name != inputs.at("relation")->name)
                    reject(proof.gives.span, "`gives` must return the relation model hole");
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

                auto named_forall = [&](char const *role, std::vector<z3::expr> const &variables,
                                        z3::expr const &body) {
                    std::vector<Z3_app> bound;
                    bound.reserve(variables.size());
                    for (z3::expr const &variable : variables)
                        bound.push_back(reinterpret_cast<Z3_app>(static_cast<Z3_ast>(variable)));
                    std::string qid = std::string("fine.bisim.") + role;
                    Z3_ast result = Z3_mk_quantifier_const_ex(
                        context_, true, 0, context_.str_symbol(qid.c_str()), context_.str_symbol(""),
                        static_cast<unsigned>(bound.size()), bound.data(), 0, nullptr, 0, nullptr, body);
                    context_.check_error();
                    return z3::expr(context_, result);
                };

                std::vector<std::pair<std::string, z3::expr>> assertions;
                assertions.emplace_back(
                    "labels-agree",
                    named_forall("labels-agree", {left, right},
                                 z3::implies(related(left, right), z3::select(left_label.value, left) ==
                                                                       z3::select(right_label.value, right))));
                assertions.emplace_back(
                    "left-step-matched",
                    named_forall("left-step-matched", {left, right, left_next},
                                 z3::implies(related(left, right) && steps_left(left, left_next),
                                             z3::exists(right_next, steps_right(right, right_next) &&
                                                                        related(left_next, right_next)))));
                assertions.emplace_back(
                    "right-step-matched",
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
                        rainfall_->source_term(
                            proof.node_id, proof.span, "decl.proof", assertion,
                            "generated", {run_scope});
                        rainfall_->record(
                            "constraint", "bisim.clause.assert", {run_scope}, "fine.bisimulation",
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
                         RainfallRecorder::raw_field("assertions",
                                                     RainfallRecorder::string_array(assertion_references)),
                         RainfallRecorder::string_field("polarity", "model-exists"),
                         RainfallRecorder::boolean_field("mbqi", true),
                         RainfallRecorder::boolean_field("ematching", false)});
                }

                std::unique_ptr<RainfallQuantifierObserver> quantifier_observer;
                std::unique_ptr<RainfallClauseObserver> clause_observer;
                if (rainfall_) {
                    quantifier_observer = std::make_unique<RainfallQuantifierObserver>(
                        solver, *rainfall_, std::vector<std::string>{run_scope, query}, false, true);
                    clause_observer = std::make_unique<RainfallClauseObserver>(
                        solver, *rainfall_, std::vector<std::string>{run_scope, query});
                }
                z3::check_result result = solver.check();
                quantifier_observer.reset();
                clause_observer.reset();
                if (rainfall_) {
                    char const *status = result == z3::sat ? "sat" : result == z3::unsat ? "unsat" : "unknown";
                    rainfall_->record(
                        "transition", "solver.query.result", {run_scope, query}, "z3.public-api",
                        "Final public check result only; no claim about solver search, MBQI steps, or internal cause",
                        {RainfallRecorder::string_field("query", query),
                         RainfallRecorder::string_field("status", status),
                         RainfallRecorder::string_field("polarity", "model-exists")});
                    rainfall_->record("scope", "solver.query.close", {run_scope, query}, "fine.bisimulation",
                                      "Public solver query lifetime", {RainfallRecorder::string_field("id", query)});
                }
                if (result != z3::sat) {
                    std::string detail =
                        result == z3::unknown ? "unknown: " + solver.reason_unknown() : "unsatisfiable";
                    reject(proof.span, "bisimulation model hole was " + detail);
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
                            reject(proof.span, "model returned a non-Boolean relation cell");
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
                    reject(proof.span, "parse(print(lift(x))) violated exact AST identity after reification");

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

            static void validate_step(Binding const &step, TypePtr const &state, syntax::SourceSpan span,
                                      std::string const &role) {
                expect_bool_range(step, span, role);
                TypePtr domain = step.type->arguments[0];
                if (domain->kind != RuntimeType::Kind::tuple || !same(domain->arguments[0], state) ||
                    !same(domain->arguments[1], state))
                    reject(span,
                           role + " must have type Table((" + state->display + ", " + state->display + "), Bool)");
            }

            static void validate_label(Binding const &label, TypePtr const &state, syntax::SourceSpan span,
                                       std::string const &role) {
                expect_bool_range(label, span, role);
                if (!same(label.type->arguments[0], state))
                    reject(span, role + " must have type Table(" + state->display + ", Bool)");
            }

            SurfaceValue lift_value(TypePtr const &type, z3::expr const &expression) const {
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

            z3::expr reify_value(TypePtr const &type, SurfaceValue const &value) {
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

            SurfaceTable lift_table(TypePtr const &type, z3::expr const &expression) const {
                SurfaceTable result;
                lift_table_into(type, expression, result);
                return result;
            }

            void lift_table_into(TypePtr const &type, z3::expr const &expression, SurfaceTable &output) const {
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
                    output.entries.push_back({lift_value(type->arguments[0], expression.arg(1)),
                                              lift_value(type->arguments[1], expression.arg(2))});
                    return;
                }
                reject({}, "array value is outside Fine's admitted table syntax");
            }

            z3::expr reify_table(TypePtr const &type, SurfaceTable const &table) {
                z3::expr result =
                    z3::const_array(type->arguments[0]->sort, reify_value(type->arguments[1], table.default_value));
                for (SurfaceEntry const &entry : table.entries)
                    result = z3::store(result, reify_value(type->arguments[0], entry.key),
                                       reify_value(type->arguments[1], entry.value));
                return result;
            }

            static void print_value(std::ostream &output, SurfaceValue const &value) {
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

            static void print_table_expression(std::ostream &output, SurfaceTable const &table) {
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

            static std::string render_model_witness(std::string const &name, TypePtr const &type,
                                                    SurfaceTable const &table) {
                std::ostringstream output;
                output << "model " << name << ": " << type->display << " = ";
                print_table_expression(output, table);
                output << ";\n";
                return output.str();
            }
        };

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
                reject(document.span,
                       "proof families currently require one ground least-relation membership `check`");
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

    }  // namespace

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
        return Runtime(output, rainfall_output, snapshot, std::move(rainfall_run)).execute(document);
    }

}  // namespace fine
