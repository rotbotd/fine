#include "runtime_internal.h"

namespace fine::runtime_detail {

    void Runtime::declare_predicate(syntax::PredicateDecl const &declaration) {
        reserve_value_name(declaration.name, declaration.span);
        std::vector<TypePtr> index_types;
        std::vector<z3::sort> index_sorts;
        std::set<std::string> index_names;
        for (syntax::Parameter const &index : declaration.indices) {
            if (!index_names.insert(index.name).second)
                reject(index.span, "duplicate predicate index `" + index.name + "`");
            TypePtr type = resolve_type(index.type);
            if (type->kind == RuntimeType::Kind::table)
                reject(index.type.span, "predicate indices must be native Fine values, not Table");
            index_types.push_back(type);
            index_sorts.push_back(type->sort);
        }

        auto info = std::make_unique<PredicateInfo>(context_);
        info->name = declaration.name;
        info->index_types = index_types;
        info->horn_complete = std::none_of(
            declaration.constructors.begin(), declaration.constructors.end(),
            [](syntax::PredicateConstructor const &constructor) { return !constructor.arbitrary_premises.empty(); });
        info->relation = context_.function(declaration.name.c_str(), static_cast<unsigned>(index_sorts.size()),
                                           index_sorts.data(), context_.bool_sort());
        if (info->horn_complete)
            fixedpoint_.register_relation(info->relation);
        PredicateInfo *stable = info.get();
        predicates_.emplace(declaration.name, std::move(info));

        std::set<std::string> constructor_names;
        std::string predicate_scope = "predicate:" + declaration.name;
        if (rainfall_)
            rainfall_->record("object", "fine.predicate.relation", {predicate_scope}, "fine.elaborator",
                              stable->horn_complete
                                  ? "Erased indexed proposition represented by a native-sort least relation"
                                  : "Erased indexed proposition represented for induction by a compiler-owned "
                                    "constructor table and relation-shaped term handle; no constructor is registered "
                                    "with fixedpoint because one has an arbitrary field",
                              {RainfallRecorder::string_field("predicate", declaration.name),
                               RainfallRecorder::string_field("relation", stable->relation.name().str()),
                               RainfallRecorder::number_field("indices", stable->index_types.size()),
                               RainfallRecorder::boolean_field("least_relation", stable->horn_complete),
                               RainfallRecorder::boolean_field("horn_complete", stable->horn_complete),
                               RainfallRecorder::boolean_field("derivation_witnesses_erased", true)});

        for (syntax::PredicateConstructor const &constructor : declaration.constructors) {
            if (!constructor_names.insert(constructor.name).second)
                reject(constructor.span, "duplicate predicate constructor `" + constructor.name + "`");

            ExpressionEnvironment environment;
            z3::expr_vector formal_parameters(context_);
            PredicateConstructorInfo retained_constructor;
            retained_constructor.name = constructor.name;
            std::set<std::string> parameter_names;
            for (std::size_t i = 0; i < constructor.parameters.size(); ++i) {
                syntax::Parameter const &parameter = constructor.parameters[i];
                if (!parameter_names.insert(parameter.name).second)
                    reject(parameter.span, "duplicate predicate-constructor parameter `" + parameter.name + "`");
                TypePtr type = resolve_type(parameter.type);
                if (type->kind == RuntimeType::Kind::table)
                    reject(parameter.type.span, "predicate-constructor parameters must be native Fine values, not Table");
                std::string internal =
                    "Fine.predicate." + declaration.name + "." + constructor.name + ".arg" + std::to_string(i);
                z3::expr term = context_.constant(internal.c_str(), type->sort);
                environment.emplace(parameter.name, TypedExpression{type, term});
                formal_parameters.push_back(term);
                retained_constructor.parameters.push_back(term);
            }

            auto atom = [&](syntax::Expr const &expression, std::string_view role) {
                if (expression.kind != syntax::Expr::Kind::call || !predicates_.contains(expression.name))
                    reject(expression.span, std::string(role) + " must be a direct call to a declared predicate");
                TypedExpression elaborated = elaborate_expression(expression, environment);
                if (!same(elaborated.type, bool_type_))
                    reject(expression.span, std::string(role) + " must be an indexed proposition");
                return elaborated.expression;
            };

            z3::expr conclusion = atom(constructor.result, "predicate-constructor result");
            if (constructor.result.name != declaration.name)
                reject(constructor.result.span,
                       "constructor `" + constructor.name + "` must produce `" + declaration.name + "(...)`");
            for (syntax::Expr const &index : constructor.result.elements)
                retained_constructor.result_indices.push_back(elaborate_expression(index, environment).expression);

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
                                               "parameter would be one-witness search, not a universal constructor field");
            }

            z3::expr premises = context_.bool_val(true);
            std::vector<z3::expr> premise_terms;
            std::size_t recursive_premises = 0;
            retained_constructor.premise_count = constructor.premises.size();
            for (syntax::Expr const &premise : constructor.premises) {
                z3::expr elaborated = atom(premise, "predicate-constructor premise");
                premises = premises && elaborated;
                premise_terms.push_back(elaborated);
                retained_constructor.premise_terms.push_back(elaborated);
                if (premise.name == declaration.name) {
                    ++recursive_premises;
                    std::vector<z3::expr> indices;
                    for (syntax::Expr const &index : premise.elements)
                        indices.push_back(elaborate_expression(index, environment).expression);
                    retained_constructor.recursive_premise_indices.push_back(std::move(indices));
                }
            }

            for (std::size_t field_ordinal = 0; field_ordinal < constructor.arbitrary_premises.size();
                 ++field_ordinal) {
                syntax::ArbitraryPremise const &field = constructor.arbitrary_premises[field_ordinal];
                bool previous_capture = capture_source_edges_;
                std::vector<std::string> previous_within = source_edge_within_;
                capture_source_edges_ = true;
                source_edge_within_ = {predicate_scope};
                if (constructor.arbitrary_premises.size() != 1)
                    reject(field.span, "the first arbitrary-fresh slice admits exactly one such field per constructor");
                if (parameter_names.contains(field.binder))
                    reject(field.span, "arbitrary-fresh name `" + field.binder + "` shadows a constructor parameter");
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
                        reject(field.view_arguments[i].span, "constrained-view argument " + std::to_string(i + 1) +
                                                                 " must have type `" +
                                                                 view.parameter_types[i]->display + "`");
                    view_arguments.push_back(std::move(argument));
                }

                PredicateConstructorInfo::ArbitraryField retained_field(context_);
                retained_field.binder = field.binder;
                retained_field.view_name = field.view_name;
                retained_field.binder_term =
                    context_.constant(("Fine.predicate." + declaration.name + "." + constructor.name + ".arbitrary" +
                                       std::to_string(field_ordinal))
                                          .c_str(),
                                      view.carrier->sort);
                if (rainfall_)
                    rainfall_->source_term(field.node_id, field.span, "predicate.arbitrary-field",
                                           retained_field.binder_term, "generated", {predicate_scope});

                ExpressionEnvironment scoped_environment = environment;
                scoped_environment.emplace(field.binder, TypedExpression{view.carrier, retained_field.binder_term});
                ExpressionEnvironment requirement_environment;
                for (std::size_t i = 0; i < view.parameter_names.size(); ++i)
                    requirement_environment.emplace(view.parameter_names[i], view_arguments[i]);
                ExpressionEnvironment witness_environment = requirement_environment;
                requirement_environment.emplace("value", TypedExpression{view.carrier, retained_field.binder_term});

                retained_field.requirement = context_.bool_val(true);
                for (syntax::Expr const &requirement : view.requirements) {
                    TypedExpression elaborated = elaborate_expression(requirement, requirement_environment);
                    if (!same(elaborated.type, bool_type_))
                        throw std::runtime_error("validated view requirement changed type");
                    retained_field.requirement = retained_field.requirement && elaborated.expression;
                }
                if (view.witness) {
                    TypedExpression witness = elaborate_expression(*view.witness, witness_environment);
                    if (!same(witness.type, view.carrier))
                        throw std::runtime_error("validated view witness changed type");
                    retained_field.availability_witness = witness.expression;
                }

                for (syntax::Expr const &scoped_premise : field.premises) {
                    z3::expr elaborated = [&] {
                        if (scoped_premise.kind != syntax::Expr::Kind::call ||
                            !predicates_.contains(scoped_premise.name))
                            reject(scoped_premise.span, "an arbitrary-fresh premise must call a declared predicate");
                        TypedExpression result = elaborate_expression(scoped_premise, scoped_environment);
                        if (!same(result.type, bool_type_))
                            reject(scoped_premise.span, "an arbitrary-fresh premise must be an indexed proposition");
                        return result.expression;
                    }();
                    if (scoped_premise.name != declaration.name)
                        reject(scoped_premise.span,
                               "the first arbitrary-fresh slice requires a recursive premise on `" + declaration.name +
                                   "`");
                    retained_field.premise_terms.push_back(elaborated);
                    std::vector<z3::expr> indices;
                    for (unsigned i = 0; i < elaborated.num_args(); ++i)
                        indices.push_back(elaborated.arg(i));
                    retained_field.recursive_premise_indices.push_back(std::move(indices));
                }

                if (rainfall_)
                    rainfall_->record(
                        "derive", "fine.predicate-constructor.arbitrary-field", {predicate_scope}, "fine.elaborator",
                        "Compiler-owned arbitrary-fresh derivation field; its view requirement and recursive premise are "
                        "retained and are deliberately not inserted into a Horn body",
                        {RainfallRecorder::string_field("predicate", declaration.name),
                         RainfallRecorder::string_field("constructor", constructor.name),
                         RainfallRecorder::number_field("field_ordinal", field_ordinal),
                         RainfallRecorder::string_field("binder", field.binder),
                         RainfallRecorder::string_field("binder_term", rainfall_->term(retained_field.binder_term)),
                         RainfallRecorder::string_field("view", field.view_name),
                         RainfallRecorder::string_field("requirement", rainfall_->term(retained_field.requirement)),
                         RainfallRecorder::string_field("availability_witness",
                                                        retained_field.availability_witness
                                                            ? rainfall_->term(*retained_field.availability_witness)
                                                            : ""),
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
                    rainfall_->source_term(declaration.node_id, declaration.span, "decl.predicate", rule,
                                           "generated", {predicate_scope});
                    std::vector<RainfallField> data{
                        RainfallRecorder::string_field("predicate", declaration.name),
                        RainfallRecorder::string_field("constructor", constructor.name),
                        RainfallRecorder::string_field("conclusion", rainfall_->term(conclusion)),
                        RainfallRecorder::number_field("premises", premise_terms.size()),
                        RainfallRecorder::number_field("recursive_premises", recursive_premises),
                        RainfallRecorder::boolean_field("lowered_to_horn", stable->horn_complete),
                        RainfallRecorder::boolean_field("derivation_witness_erased", true)};
                    data.push_back(stable->horn_complete
                                       ? RainfallRecorder::string_field("rule", rainfall_->term(rule))
                                       : RainfallRecorder::string_field("branch_schema", rainfall_->term(rule)));
                    rainfall_->record(
                        "derive",
                        stable->horn_complete ? "fine.predicate-constructor.rule" : "fine.predicate-constructor.branch",
                        {predicate_scope}, "fine.elaborator",
                        stable->horn_complete
                            ? "Strictly-positive first-order constructor compiled to one least-relation Horn rule"
                            : "First-order constructor retained for compiler-owned induction but not registered as a "
                              "Horn rule because another constructor has an arbitrary field",
                        data);
                }
            }
            stable->constructors.push_back(std::move(retained_constructor));
        }
    }

    int Runtime::execute_predicate_check(syntax::CheckDecl const &declaration) {
        reserve_value_name(declaration.name, declaration.span);
        if (declaration.induction_parameter)
            reject(*declaration.induction_span, "predicate membership does not yet implement derivation induction");
        if (declaration.predicate_induction)
            return execute_predicate_induction(declaration);
        if (declaration.reusable)
            reject(declaration.span,
                   "a proof declared after predicates must use `inducts(P(...))`; Fine admits only the exact theorem "
                   "verified by all compiler-owned constructor branches and never adds it to fixedpoint");
        if (!declaration.parameters.empty())
            return execute_predicate_invariant(declaration);
        if (!declaration.assumes.empty())
            reject(declaration.span, "a least-relation membership check cannot yet mix ordinary assumptions");
        if (declaration.ensures.size() != 1)
            reject(declaration.span, "a least-relation membership check needs exactly one ensured atom");
        syntax::Expr const &source_query = declaration.ensures.front();
        if (source_query.kind != syntax::Expr::Kind::call || !predicates_.contains(source_query.name))
            reject(source_query.span, "the ensured condition must be a direct predicate call");
        if (!predicates_.at(source_query.name)->horn_complete)
            reject(source_query.span, "least-relation membership is unavailable because `" + source_query.name +
                                          "` has an arbitrary-fresh constructor retained outside Horn lowering");

        std::string run_scope = "predicate-check:" + declaration.name;
        capture_source_edges_ = true;
        source_edge_within_ = {run_scope};
        ExpressionEnvironment environment;
        TypedExpression elaborated = elaborate_expression(source_query, environment);
        capture_source_edges_ = false;
        source_edge_within_.clear();
        z3::expr query = elaborated.expression;

        std::unique_ptr<RainfallFixedpointObserver> fixedpoint_observer;
        if (rainfall_) {
            rainfall_->record("scope", "predicate-check.run.open", {run_scope}, "fine.fixedpoint",
                              "One public least-relation membership query; records admitted constructor rules, public "
                              "Spacer callback boundaries, and the public result",
                              {RainfallRecorder::string_field("declaration", declaration.name),
                               RainfallRecorder::string_field("predicate", source_query.name),
                               RainfallRecorder::string_field("query", rainfall_->term(query)),
                               RainfallRecorder::boolean_field("ground", true)});
            z3::params parameters(context_);
            parameters.set("engine", "spacer");
            parameters.set("spacer.p3.share_invariants", true);
            parameters.set("spacer.p3.share_lemmas", true);
            fixedpoint_.set(parameters);
            fixedpoint_observer = std::make_unique<RainfallFixedpointObserver>(fixedpoint_, *rainfall_,
                                                                               std::vector<std::string>{run_scope});
        }

        z3::check_result result = fixedpoint_.query(query);
        if (fixedpoint_observer)
            fixedpoint_observer->rethrow_if_failed();
        if (result == z3::unknown)
            reject(source_query.span, "least-relation membership was unknown: " + fixedpoint_.reason_unknown());
        bool derived = result == z3::sat;
        if (rainfall_) {
            z3::expr answer = fixedpoint_.get_answer();
            rainfall_->record("transition", "solver.fixedpoint.result", {run_scope}, "z3.public-api",
                              "Final public fixedpoint result only; no claim about internal rule-search steps",
                              {RainfallRecorder::string_field("query", rainfall_->term(query)),
                               RainfallRecorder::string_field("answer", rainfall_->term(answer)),
                               RainfallRecorder::string_field("status", derived ? "sat" : "unsat"),
                               RainfallRecorder::string_field("domain_outcome", derived ? "derived" : "not-derived")});
            rainfall_->validate_terms();
            rainfall_->record("scope", "predicate-check.run.close", {run_scope}, "fine.fixedpoint",
                              "Ground least-relation membership completed",
                              {RainfallRecorder::string_field("status", derived ? "derived" : "not-derived")});
        }
        output_ << (derived ? "derived: " : "not-derived: ") << declaration.name << '\n';
        output_ << "predicate: " << source_query.name << '\n';
        output_ << "derivation-witness: erased\n";
        return 0;
    }

    int Runtime::execute_predicate_induction(syntax::CheckDecl const &declaration) {
        syntax::Expr const &target = *declaration.predicate_induction;
        if (!predicates_.contains(target.name))
            reject(target.span, "`inducts` target must call a declared predicate");
        if (declaration.assumes.empty())
            reject(declaration.span, "predicate induction needs its target atom first in `assumes`");
        syntax::Expr const &assumed = declaration.assumes.front();
        if (assumed.kind != syntax::Expr::Kind::call || assumed.name != target.name)
            reject(assumed.span, "predicate induction assumption must be the same predicate as `inducts`");

        PredicateInfo const &predicate = *predicates_.at(target.name);
        std::size_t index_count = predicate.index_types.size();
        if (declaration.parameters.size() < index_count || target.elements.size() != index_count)
            reject(target.span,
                   "predicate induction needs one leading check parameter per predicate index; later parameters are "
                   "generalized context");

        ExpressionEnvironment environment;
        z3::expr_vector index_terms(context_);
        z3::expr_vector context_terms(context_);
        std::set<std::string> parameter_names;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const &parameter = declaration.parameters[i];
            if (!parameter_names.insert(parameter.name).second)
                reject(parameter.span, "duplicate check parameter `" + parameter.name + "`");
            TypePtr type = resolve_type(parameter.type);
            if (i < index_count && !same(type, predicate.index_types[i]))
                reject(parameter.type.span, "predicate-induction parameter " + std::to_string(i + 1) + " must have type `" +
                                                predicate.index_types[i]->display + "`");
            if (i < index_count &&
                (target.elements[i].kind != syntax::Expr::Kind::name || target.elements[i].name != parameter.name))
                reject(target.elements[i].span,
                       "predicate-induction indices must be the leading check parameters in declaration order");
            std::string internal = "Fine.predicate-induction." + declaration.name + ".arg" + std::to_string(i);
            z3::expr term = context_.constant(internal.c_str(), type->sort);
            environment.emplace(parameter.name, TypedExpression{type, term});
            if (i < index_count)
                index_terms.push_back(term);
            else
                context_terms.push_back(term);
        }

        auto direct_predicate = [&](syntax::Expr const &expression) -> PredicateInfo const * {
            if (expression.kind != syntax::Expr::Kind::call)
                return nullptr;
            auto found = predicates_.find(expression.name);
            return found == predicates_.end() ? nullptr : found->second.get();
        };
        std::function<bool(syntax::Expr const &)> contains_predicate = [&](syntax::Expr const &expression) {
            if (direct_predicate(expression))
                return true;
            return std::any_of(expression.elements.begin(), expression.elements.end(), contains_predicate);
        };

        std::vector<PredicateInfo const *> auxiliary_predicates;
        for (std::size_t i = 1; i < declaration.assumes.size(); ++i) {
            syntax::Expr const &condition = declaration.assumes[i];
            PredicateInfo const *item = direct_predicate(condition);
            if (!item && contains_predicate(condition))
                reject(condition.span,
                       "a predicate-induction context may use predicate evidence only as a direct assumed atom");
            auxiliary_predicates.push_back(item);
        }

        PredicateInfo const *goal_predicate = nullptr;
        for (syntax::Expr const &condition : declaration.ensures) {
            if (!contains_predicate(condition))
                continue;
            if (declaration.ensures.size() != 1 || !(goal_predicate = direct_predicate(condition)))
                reject(condition.span,
                       "predicate induction admits a predicate guarantee only as its one direct ensured atom");
        }

        std::string run_scope =
            std::string(declaration.reusable ? "proof:" : "predicate-induction:") + declaration.name;
        capture_source_edges_ = true;
        source_edge_within_ = {run_scope};
        TypedExpression target_term = elaborate_expression(target, environment);
        TypedExpression assumed_term = elaborate_expression(assumed, environment);
        if (!Z3_is_eq_ast(context_, target_term.expression, assumed_term.expression))
            reject(assumed.span, "`inducts` target and assumed predicate atom must elaborate identically");
        z3::expr auxiliary_assumptions = context_.bool_val(true);
        std::vector<z3::expr> auxiliary_terms;
        for (std::size_t i = 1; i < declaration.assumes.size(); ++i) {
            syntax::Expr const &condition = declaration.assumes[i];
            TypedExpression elaborated = elaborate_expression(condition, environment);
            if (!same(elaborated.type, bool_type_))
                reject(condition.span, "predicate-induction context assumption must have type Bool");
            auxiliary_assumptions = auxiliary_assumptions && elaborated.expression;
            auxiliary_terms.push_back(elaborated.expression);
        }
        z3::expr guarantees = context_.bool_val(true);
        std::optional<z3::expr> predicate_goal_atom;
        for (syntax::Expr const &condition : declaration.ensures) {
            TypedExpression elaborated = elaborate_expression(condition, environment);
            if (!same(elaborated.type, bool_type_))
                reject(condition.span, "predicate-induction guarantee must have type Bool");
            guarantees = guarantees && elaborated.expression;
            if (goal_predicate)
                predicate_goal_atom = elaborated.expression;
        }
        capture_source_edges_ = false;
        source_edge_within_.clear();

        if (rainfall_)
            rainfall_->record(
                "scope", declaration.reusable ? "proof.run.open" : "predicate-induction.run.open", {run_scope},
                "fine.induction",
                "Fine-owned structural induction over predicate constructors; each recursive predicate premise becomes "
                "one exact induction hypothesis and each branch is checked by a separate public SMT query",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::string_field("predicate", predicate.name),
                 RainfallRecorder::number_field("constructors", predicate.constructors.size()),
                 RainfallRecorder::number_field("context_parameters", context_terms.size()),
                 RainfallRecorder::number_field("context_assumptions", declaration.assumes.size() - 1),
                 RainfallRecorder::number_field(
                     "predicate_assumptions",
                     std::count_if(auxiliary_predicates.begin(), auxiliary_predicates.end(),
                                   [](PredicateInfo const *item) { return item != nullptr; })),
                 RainfallRecorder::string_field("predicate_guarantee", goal_predicate ? goal_predicate->name : ""),
                 RainfallRecorder::string_field("target", rainfall_->term(target_term.expression)),
                 RainfallRecorder::string_field("auxiliary_assumptions", rainfall_->term(auxiliary_assumptions)),
                 RainfallRecorder::string_field("guarantees", rainfall_->term(guarantees))});

        z3::expr admitted_body = z3::implies(target_term.expression && auxiliary_assumptions, guarantees);

        auto instantiate = [&](z3::expr const &expression, std::vector<z3::expr> const &indices) {
            if (indices.size() != index_terms.size())
                throw std::runtime_error("internal predicate index arity mismatch");
            z3::expr_vector replacements(context_);
            for (z3::expr const &index : indices)
                replacements.push_back(index);
            z3::expr result = expression;
            return result.substitute(index_terms, replacements);
        };

        z3::expr contextual_theorem = z3::implies(auxiliary_assumptions, guarantees);
        auto induction_hypothesis = [&](std::vector<z3::expr> const &indices, std::string const &suffix) {
            if (context_terms.empty())
                return declaration.assumes.size() == 1 ? instantiate(guarantees, indices)
                                                       : instantiate(contextual_theorem, indices);

            z3::expr hypothesis = instantiate(contextual_theorem, indices);
            z3::expr_vector source(context_);
            z3::expr_vector destination(context_);
            std::vector<Z3_app> bound;
            for (unsigned i = 0; i < context_terms.size(); ++i) {
                z3::expr const &term = context_terms[i];
                std::string name = "Fine.predicate-induction." + declaration.name + ".ih." + suffix + ".context" +
                                   std::to_string(i);
                z3::expr generalized = context_.constant(name.c_str(), term.get_sort());
                source.push_back(term);
                destination.push_back(generalized);
                bound.push_back(reinterpret_cast<Z3_app>(static_cast<Z3_ast>(generalized)));
            }
            hypothesis = hypothesis.substitute(source, destination);
            std::string qid = "fine.predicate-induction." + declaration.name + "." + suffix;
            Z3_ast quantified = Z3_mk_quantifier_const_ex(
                context_, true, 0, context_.str_symbol(qid.c_str()), context_.str_symbol(""),
                static_cast<unsigned>(bound.size()), bound.data(), 0, nullptr, 0, nullptr, hypothesis);
            context_.check_error();
            return z3::expr(context_, quantified);
        };

        auto field_availability = [&](PredicateConstructorInfo const &constructor,
                                      PredicateConstructorInfo::ArbitraryField const &field,
                                      bool close_constructor_parameters) {
            z3::expr availability = field.requirement;
            if (field.availability_witness) {
                z3::expr_vector source(context_);
                z3::expr_vector destination(context_);
                source.push_back(field.binder_term);
                destination.push_back(*field.availability_witness);
                availability = availability.substitute(source, destination);
            }
            else {
                z3::expr_vector binder(context_);
                binder.push_back(field.binder_term);
                availability = z3::exists(binder, availability);
            }
            if (close_constructor_parameters && !constructor.parameters.empty()) {
                z3::expr_vector parameters(context_);
                for (z3::expr const &parameter : constructor.parameters)
                    parameters.push_back(parameter);
                availability = z3::forall(parameters, availability);
            }
            return availability;
        };

        std::vector<PredicateInfo const *> one_layer_predicates;
        for (PredicateInfo const *item : auxiliary_predicates) {
            if (item && std::find(one_layer_predicates.begin(), one_layer_predicates.end(), item) ==
                            one_layer_predicates.end())
                one_layer_predicates.push_back(item);
        }
        if (goal_predicate && std::find(one_layer_predicates.begin(), one_layer_predicates.end(), goal_predicate) ==
                                  one_layer_predicates.end())
            one_layer_predicates.push_back(goal_predicate);
        for (PredicateInfo const *item : one_layer_predicates) {
            for (PredicateConstructorInfo const &constructor : item->constructors) {
                for (std::size_t field_ordinal = 0; field_ordinal < constructor.arbitrary_fields.size();
                     ++field_ordinal) {
                    PredicateConstructorInfo::ArbitraryField const &field =
                        constructor.arbitrary_fields[field_ordinal];
                    z3::expr obligation = field_availability(constructor, field, true);
                    z3::solver solver(context_);
                    for (AdmittedProof const &proof : admitted_proofs_) {
                        solver.add(proof.theorem);
                        if (rainfall_)
                            rainfall_->record(
                                "constraint", "proof.use", {run_scope}, "fine.induction",
                                "Previously verified source proof admitted to a secondary predicate field's "
                                "availability query",
                                {RainfallRecorder::string_field("proof", proof.name),
                                 RainfallRecorder::string_field("theorem", rainfall_->term(proof.theorem)),
                                 RainfallRecorder::string_field("consumer", declaration.name),
                                 RainfallRecorder::string_field("predicate", item->name),
                                 RainfallRecorder::string_field("constructor", constructor.name),
                                 RainfallRecorder::string_field("phase", "one-layer-availability")});
                    }
                    solver.add(!obligation);
                    z3::check_result result = solver.check();
                    if (result == z3::unknown)
                        reject(target.span, "constrained-field availability for `" + item->name + "." +
                                                constructor.name + "` was unknown: " + solver.reason_unknown());
                    bool available = result == z3::unsat;
                    if (rainfall_)
                        rainfall_->record(
                            "transition", "predicate-induction.one-layer.availability", {run_scope},
                            "z3.public-api",
                            "Availability of one total constrained field before its predicate is used for "
                            "one-layer inversion or construction",
                            {RainfallRecorder::string_field("predicate", item->name),
                             RainfallRecorder::string_field("constructor", constructor.name),
                             RainfallRecorder::number_field("field_ordinal", field_ordinal),
                             RainfallRecorder::string_field("binder", field.binder),
                             RainfallRecorder::string_field("view", field.view_name),
                             RainfallRecorder::string_field(
                                 "availability_mode",
                                 field.availability_witness ? "declared-witness" : "solver-exists"),
                             RainfallRecorder::string_field(
                                 "availability_witness",
                                 field.availability_witness ? rainfall_->term(*field.availability_witness) : ""),
                             RainfallRecorder::string_field("obligation", rainfall_->term(obligation)),
                             RainfallRecorder::string_field("status", available ? "unsat" : "sat"),
                             RainfallRecorder::string_field(
                                 "domain_outcome",
                                 available ? "available"
                                           : (field.availability_witness ? "invalid-witness" : "empty"))});
                    if (!available)
                        reject(target.span, "constrained field `" + item->name + "." + constructor.name + "." +
                                                field.binder +
                                                "` is unavailable; one-layer predicate use would be vacuous");
                }
            }
        }

        auto one_layer = [&](PredicateInfo const &item, z3::expr const &atom,
                             std::string const &consumer_constructor, std::string const &use,
                             std::size_t assumption_ordinal) {
            if (!atom.is_app() || atom.num_args() != item.index_types.size())
                throw std::runtime_error("internal predicate inversion arity mismatch");
            z3::expr alternatives = context_.bool_val(false);
            for (PredicateConstructorInfo const &constructor : item.constructors) {
                z3::expr branch = context_.bool_val(true);
                for (unsigned i = 0; i < atom.num_args(); ++i)
                    branch = branch && atom.arg(i) == constructor.result_indices[i];
                for (z3::expr const &premise : constructor.premise_terms)
                    branch = branch && premise;
                for (std::size_t field_ordinal = 0; field_ordinal < constructor.arbitrary_fields.size();
                     ++field_ordinal) {
                    PredicateConstructorInfo::ArbitraryField const &field =
                        constructor.arbitrary_fields[field_ordinal];
                    z3::expr field_premises = context_.bool_val(true);
                    for (z3::expr const &premise : field.premise_terms)
                        field_premises = field_premises && premise;
                    z3::expr_vector binder(context_);
                    binder.push_back(field.binder_term);
                    z3::expr availability = field_availability(constructor, field, false);
                    z3::expr total_field = z3::forall(binder, z3::implies(field.requirement, field_premises));
                    branch = branch && availability && total_field;
                    if (rainfall_) {
                        std::vector<RainfallField> data{
                            RainfallRecorder::string_field("consumer_constructor", consumer_constructor),
                            RainfallRecorder::string_field("use", use),
                            RainfallRecorder::string_field("predicate", item.name),
                            RainfallRecorder::string_field("predicate_constructor", constructor.name),
                            RainfallRecorder::number_field("field_ordinal", field_ordinal),
                            RainfallRecorder::string_field("binder", field.binder),
                            RainfallRecorder::string_field("binder_term", rainfall_->term(field.binder_term)),
                            RainfallRecorder::string_field("view", field.view_name),
                            RainfallRecorder::string_field("requirement", rainfall_->term(field.requirement)),
                            RainfallRecorder::string_field(
                                "availability_mode", field.availability_witness ? "declared-witness" : "solver-exists"),
                            RainfallRecorder::string_field(
                                "availability_witness",
                                field.availability_witness ? rainfall_->term(*field.availability_witness) : ""),
                            RainfallRecorder::string_field("availability", rainfall_->term(availability)),
                            RainfallRecorder::string_field("premises", rainfall_->term(field_premises)),
                            RainfallRecorder::string_field("total_field", rainfall_->term(total_field)),
                            RainfallRecorder::number_field("recursive_premises", field.premise_terms.size())};
                        if (use == "assumption")
                            data.push_back(RainfallRecorder::number_field("assumption_ordinal", assumption_ordinal));
                        rainfall_->record(
                            "derive", "predicate-induction." + use + ".arbitrary-field",
                            {run_scope, "branch:" + consumer_constructor}, "fine.induction",
                            "Compiler-owned total constrained field in one predicate-constructor alternative; "
                            "availability and the universally scoped recursive premises remain separate exact terms",
                            data);
                    }
                }
                if (!constructor.parameters.empty()) {
                    z3::expr_vector parameters(context_);
                    for (z3::expr const &parameter : constructor.parameters)
                        parameters.push_back(parameter);
                    branch = z3::exists(parameters, branch);
                }
                alternatives = alternatives || branch;
            }
            return alternatives;
        };

        std::string failed_branch;
        for (PredicateConstructorInfo const &constructor : predicate.constructors) {
            if (constructor.premise_count != constructor.recursive_premise_indices.size())
                reject(target.span, "predicate induction currently requires every constructor premise to recurse on `" +
                                        predicate.name + "`");
            std::string branch_scope = "branch:" + constructor.name;
            z3::expr constructor_result = predicate.relation(static_cast<unsigned>(constructor.result_indices.size()),
                                                          constructor.result_indices.data());
            z3::expr branch_goal = instantiate(guarantees, constructor.result_indices);
            z3::expr branch_goal_atom =
                goal_predicate ? instantiate(*predicate_goal_atom, constructor.result_indices) : branch_goal;
            z3::expr branch_goal_resource = goal_predicate
                                                ? one_layer(*goal_predicate, branch_goal_atom, constructor.name,
                                                            "goal", 0)
                                                : branch_goal;
            if (goal_predicate && rainfall_)
                rainfall_->record(
                    "derive", "predicate-induction.goal.construct", {run_scope, branch_scope}, "fine.induction",
                    "Compiler-owned one-constructor construction obligation for a positive predicate goal; this "
                    "bounded unfolding avoids a recursive universal introduction axiom",
                    {RainfallRecorder::string_field("constructor", constructor.name),
                     RainfallRecorder::string_field("predicate", goal_predicate->name),
                     RainfallRecorder::string_field("goal", rainfall_->term(branch_goal_atom)),
                     RainfallRecorder::string_field("construction", rainfall_->term(branch_goal_resource)),
                     RainfallRecorder::number_field("alternatives", goal_predicate->constructors.size())});
            z3::expr branch_assumptions = instantiate(auxiliary_assumptions, constructor.result_indices);
            z3::expr branch_resources = context_.bool_val(true);
            std::size_t inverted_assumptions = 0;
            for (std::size_t i = 0; i < auxiliary_terms.size(); ++i) {
                z3::expr specialized = instantiate(auxiliary_terms[i], constructor.result_indices);
                z3::expr resource = specialized;
                if (PredicateInfo const *assumption_predicate = auxiliary_predicates[i]) {
                    z3::expr unfolded =
                        one_layer(*assumption_predicate, specialized, constructor.name, "assumption", i);
                    resource = specialized && unfolded;
                    ++inverted_assumptions;
                    if (rainfall_)
                        rainfall_->record(
                            "derive", "predicate-induction.assumption.invert", {run_scope, branch_scope},
                            "fine.induction",
                            "Compiler-owned one-constructor unfolding of a positive predicate assumption under its "
                            "least constructor-generated interpretation",
                            {RainfallRecorder::string_field("constructor", constructor.name),
                             RainfallRecorder::number_field("assumption_ordinal", i),
                             RainfallRecorder::string_field("predicate", assumption_predicate->name),
                             RainfallRecorder::string_field("assumption", rainfall_->term(specialized)),
                             RainfallRecorder::string_field("inversion", rainfall_->term(unfolded)),
                             RainfallRecorder::string_field("resource", rainfall_->term(resource)),
                             RainfallRecorder::number_field("alternatives",
                                                            assumption_predicate->constructors.size())});
                }
                branch_resources = branch_resources && resource;
            }
            z3::expr hypotheses = context_.bool_val(true);
            std::vector<z3::expr> hypothesis_terms;
            for (std::size_t i = 0; i < constructor.recursive_premise_indices.size(); ++i) {
                std::vector<z3::expr> const &indices = constructor.recursive_premise_indices[i];
                z3::expr hypothesis = induction_hypothesis(
                    indices, constructor.name + ".premise" + std::to_string(i));
                hypotheses = hypotheses && hypothesis;
                hypothesis_terms.push_back(hypothesis);
                if (rainfall_) {
                    z3::expr premise = predicate.relation(static_cast<unsigned>(indices.size()), indices.data());
                    rainfall_->record(
                        "derive", "predicate-induction.hypothesis", {run_scope, branch_scope}, "fine.induction",
                        "Compiler-owned recursive constructor premise paired with the guarantee instantiated at its "
                        "exact indices",
                        {RainfallRecorder::string_field("constructor", constructor.name),
                         RainfallRecorder::number_field("premise_ordinal", i),
                         RainfallRecorder::string_field("recursive_premise", rainfall_->term(premise)),
                         RainfallRecorder::string_field("induction_hypothesis", rainfall_->term(hypothesis)),
                         RainfallRecorder::number_field("generalized_parameters", context_terms.size())});
                }
            }

            for (std::size_t field_ordinal = 0; field_ordinal < constructor.arbitrary_fields.size(); ++field_ordinal) {
                PredicateConstructorInfo::ArbitraryField const &field = constructor.arbitrary_fields[field_ordinal];

                z3::expr branch_availability = field.requirement;
                if (field.availability_witness) {
                    z3::expr_vector source(context_);
                    z3::expr_vector destination(context_);
                    source.push_back(field.binder_term);
                    destination.push_back(*field.availability_witness);
                    branch_availability = branch_availability.substitute(source, destination);
                }
                else {
                    z3::expr_vector binders(context_);
                    binders.push_back(field.binder_term);
                    branch_availability = z3::exists(binders, branch_availability);
                }
                z3::expr availability = branch_availability;
                if (!constructor.parameters.empty()) {
                    z3::expr_vector constructor_parameters(context_);
                    for (z3::expr const &parameter : constructor.parameters)
                        constructor_parameters.push_back(parameter);
                    availability = z3::forall(constructor_parameters, availability);
                }
                z3::solver availability_solver(context_);
                for (AdmittedProof const &proof : admitted_proofs_) {
                    availability_solver.add(proof.theorem);
                    if (rainfall_)
                        rainfall_->record(
                            "constraint", "proof.use", {run_scope, branch_scope}, "fine.induction",
                            "Previously verified source proof admitted to the arbitrary-field availability query",
                            {RainfallRecorder::string_field("proof", proof.name),
                             RainfallRecorder::string_field("theorem", rainfall_->term(proof.theorem)),
                             RainfallRecorder::string_field("consumer", declaration.name),
                             RainfallRecorder::string_field("constructor", constructor.name),
                             RainfallRecorder::string_field("phase", "arbitrary-availability")});
                }
                availability_solver.add(!availability);
                z3::check_result availability_result = availability_solver.check();
                if (availability_result == z3::unknown)
                    reject(target.span, "arbitrary-fresh availability for constructor `" + constructor.name +
                                            "` was unknown: " + availability_solver.reason_unknown());
                bool available = availability_result == z3::unsat;
                if (rainfall_)
                    rainfall_->record(
                        "transition", "predicate-induction.arbitrary.availability", {run_scope, branch_scope},
                        "z3.public-api",
                        "Fine separately checks that every constructor-parameter assignment admits a carrier value "
                        "satisfying the constrained view; this prevents vacuous arbitrary-fresh branches",
                        {RainfallRecorder::string_field("constructor", constructor.name),
                         RainfallRecorder::number_field("field_ordinal", field_ordinal),
                         RainfallRecorder::string_field("binder", field.binder),
                         RainfallRecorder::string_field("view", field.view_name),
                         RainfallRecorder::string_field("requirement", rainfall_->term(field.requirement)),
                         RainfallRecorder::string_field(
                             "availability_mode", field.availability_witness ? "declared-witness" : "solver-exists"),
                         RainfallRecorder::string_field(
                             "availability_witness",
                             field.availability_witness ? rainfall_->term(*field.availability_witness) : ""),
                         RainfallRecorder::string_field("obligation", rainfall_->term(availability)),
                         RainfallRecorder::string_field("status", available ? "unsat" : "sat"),
                         RainfallRecorder::string_field(
                             "domain_outcome",
                             available ? "available" : (field.availability_witness ? "invalid-witness" : "empty"))});
                if (!available) {
                    if (field.availability_witness)
                        reject(target.span, "declared witness for constrained view `" + field.view_name +
                                                "` fails its requirement for some parameters of `" + constructor.name +
                                                "`");
                    reject(target.span, "constrained view `" + field.view_name + "` is empty for some parameters of `" +
                                            constructor.name + "`; arbitrary-fresh induction would be vacuous");
                }

                z3::expr field_hypotheses = context_.bool_val(true);
                for (std::size_t i = 0; i < field.recursive_premise_indices.size(); ++i) {
                    std::vector<z3::expr> const &indices = field.recursive_premise_indices[i];
                    z3::expr hypothesis = induction_hypothesis(
                        indices, constructor.name + ".arbitrary" + std::to_string(field_ordinal) + ".premise" +
                                     std::to_string(i));
                    field_hypotheses = field_hypotheses && hypothesis;
                    hypothesis_terms.push_back(hypothesis);
                    if (rainfall_)
                        rainfall_->record(
                            "derive", "predicate-induction.arbitrary-hypothesis", {run_scope, branch_scope},
                            "fine.induction",
                            "Exact recursive premise and induction-hypothesis template owned by one total "
                            "arbitrary constrained field",
                            {RainfallRecorder::string_field("constructor", constructor.name),
                             RainfallRecorder::number_field("field_ordinal", field_ordinal),
                             RainfallRecorder::number_field("premise_ordinal", i),
                             RainfallRecorder::string_field("binder", field.binder),
                             RainfallRecorder::string_field("binder_term", rainfall_->term(field.binder_term)),
                             RainfallRecorder::string_field("view", field.view_name),
                             RainfallRecorder::string_field("requirement", rainfall_->term(field.requirement)),
                             RainfallRecorder::string_field("recursive_premise",
                                                            rainfall_->term(field.premise_terms[i])),
                             RainfallRecorder::string_field("induction_hypothesis", rainfall_->term(hypothesis)),
                             RainfallRecorder::number_field("generalized_parameters", context_terms.size()),
                             RainfallRecorder::string_field("scope_owner", "fine-arbitrary-field")});
                }
                z3::expr_vector binder(context_);
                binder.push_back(field.binder_term);
                z3::expr total_hypotheses =
                    z3::forall(binder, z3::implies(field.requirement, field_hypotheses));
                hypotheses = hypotheses && branch_availability && total_hypotheses;
                if (rainfall_)
                    rainfall_->record(
                        "derive", "predicate-induction.arbitrary.total-hypothesis", {run_scope, branch_scope},
                        "fine.induction",
                        "Compiler-owned total induction resource for every carrier satisfying one constrained "
                        "field; the separately verified availability resource prevents vacuity",
                        {RainfallRecorder::string_field("constructor", constructor.name),
                         RainfallRecorder::number_field("field_ordinal", field_ordinal),
                         RainfallRecorder::string_field("binder", field.binder),
                         RainfallRecorder::string_field("binder_term", rainfall_->term(field.binder_term)),
                         RainfallRecorder::string_field("view", field.view_name),
                         RainfallRecorder::string_field("requirement", rainfall_->term(field.requirement)),
                         RainfallRecorder::string_field("availability_resource",
                                                        rainfall_->term(branch_availability)),
                         RainfallRecorder::string_field("hypothesis_templates",
                                                        rainfall_->term(field_hypotheses)),
                         RainfallRecorder::string_field("total_hypothesis", rainfall_->term(total_hypotheses)),
                         RainfallRecorder::number_field("recursive_hypotheses",
                                                        field.recursive_premise_indices.size()),
                         RainfallRecorder::string_field("scope_owner", "fine-arbitrary-field")});
            }
            z3::expr branch_query = hypotheses && branch_resources && !branch_goal_resource;
            if (rainfall_) {
                rainfall_->source_term(declaration.node_id, declaration.span,
                                       declaration.reusable ? "decl.proof" : "decl.check", branch_query, "generated",
                                       {run_scope, branch_scope});
                rainfall_->record(
                    "scope", "predicate-induction.branch.open", {run_scope, branch_scope}, "fine.induction",
                    "One constructor branch with compiler-owned result indices and recursive hypotheses",
                    {RainfallRecorder::string_field("constructor", constructor.name),
                     RainfallRecorder::number_field("constructor_parameters", constructor.parameters.size()),
                     RainfallRecorder::string_field("constructor_result", rainfall_->term(constructor_result)),
                     RainfallRecorder::string_field("context_assumptions", rainfall_->term(branch_assumptions)),
                     RainfallRecorder::string_field("context_resources", rainfall_->term(branch_resources)),
                     RainfallRecorder::number_field("inverted_assumptions", inverted_assumptions),
                     RainfallRecorder::string_field("goal", rainfall_->term(branch_goal_atom)),
                     RainfallRecorder::string_field("goal_resource", rainfall_->term(branch_goal_resource)),
                     RainfallRecorder::number_field("goal_constructor_alternatives",
                                                    goal_predicate ? goal_predicate->constructors.size() : 0),
                     RainfallRecorder::string_field("counterexample_query", rainfall_->term(branch_query)),
                     RainfallRecorder::number_field("recursive_hypotheses", hypothesis_terms.size()),
                     RainfallRecorder::number_field("arbitrary_fields", constructor.arbitrary_fields.size())});
            }

            z3::solver solver(context_);
            for (AdmittedProof const &proof : admitted_proofs_) {
                solver.add(proof.theorem);
                if (rainfall_)
                    rainfall_->record(
                        "constraint", "proof.use", {run_scope, branch_scope}, "fine.induction",
                        "Previously verified source proof admitted to this constructor branch as a universal SMT "
                        "assumption",
                        {RainfallRecorder::string_field("proof", proof.name),
                         RainfallRecorder::string_field("theorem", rainfall_->term(proof.theorem)),
                         RainfallRecorder::string_field("consumer", declaration.name),
                         RainfallRecorder::string_field("constructor", constructor.name)});
            }
            solver.add(branch_query);
            z3::check_result result = solver.check();
            if (result == z3::unknown)
                reject(target.span,
                       "predicate-induction branch `" + constructor.name + "` was unknown: " + solver.reason_unknown());
            bool branch_verified = result == z3::unsat;
            if (rainfall_) {
                rainfall_->record(
                    "transition", "predicate-induction.branch.result", {run_scope, branch_scope}, "z3.public-api",
                    "Final public SMT result for one compiler-generated constructor branch",
                    {RainfallRecorder::string_field("constructor", constructor.name),
                     RainfallRecorder::string_field("status", branch_verified ? "unsat" : "sat"),
                     RainfallRecorder::string_field("domain_outcome", branch_verified ? "verified" : "refuted")});
                rainfall_->record("scope", "predicate-induction.branch.close", {run_scope, branch_scope}, "fine.induction",
                                  "Constructor induction branch completed",
                                  {RainfallRecorder::string_field("status", branch_verified ? "verified" : "refuted")});
            }
            if (!branch_verified) {
                failed_branch = constructor.name;
                break;
            }
        }

        bool verified = failed_branch.empty();
        if (verified && declaration.reusable) {
            std::vector<Z3_app> bound;
            bound.reserve(index_terms.size() + context_terms.size());
            for (unsigned i = 0; i < index_terms.size(); ++i)
                bound.push_back(reinterpret_cast<Z3_app>(static_cast<Z3_ast>(index_terms[i])));
            for (unsigned i = 0; i < context_terms.size(); ++i)
                bound.push_back(reinterpret_cast<Z3_app>(static_cast<Z3_ast>(context_terms[i])));
            std::string qid = "fine.proof." + declaration.name;
            z3::expr admitted = admitted_body;
            if (!bound.empty()) {
                Z3_ast quantified = Z3_mk_quantifier_const_ex(
                    context_, true, 0, context_.str_symbol(qid.c_str()), context_.str_symbol(""),
                    static_cast<unsigned>(bound.size()), bound.data(), 0, nullptr, 0, nullptr, admitted_body);
                context_.check_error();
                admitted = z3::expr(context_, quantified);
            }
            admitted_proofs_.push_back({declaration.name, admitted});
            if (rainfall_)
                rainfall_->record(
                    "transition", "proof.admit", {run_scope}, "fine.induction",
                    "Only verification of every compiler-owned predicate-constructor branch permits universal "
                    "closure and later SMT reuse; the theorem is never added to fixedpoint",
                    {RainfallRecorder::string_field("proof", declaration.name),
                     RainfallRecorder::string_field("predicate", predicate.name),
                     RainfallRecorder::string_field("theorem", rainfall_->term(admitted)),
                     RainfallRecorder::string_field("qid", qid),
                     RainfallRecorder::number_field("verified_constructor_branches", predicate.constructors.size()),
                     RainfallRecorder::boolean_field("verified_before_admission", true),
                     RainfallRecorder::boolean_field("added_to_fixedpoint", false)});
        }
        if (rainfall_) {
            rainfall_->validate_terms();
            rainfall_->record("scope",
                              declaration.reusable ? "proof.run.close" : "predicate-induction.run.close",
                              {run_scope}, "fine.induction",
                              "Compiler-generated predicate induction completed",
                              {RainfallRecorder::string_field("status", verified ? "verified" : "refuted"),
                               RainfallRecorder::string_field("failed_constructor", failed_branch)});
        }
        if (declaration.reusable)
            output_ << (verified ? "verified-proof: " : "refuted-proof: ") << declaration.name << '\n';
        else
            output_ << (verified ? "verified-predicate-induction: " : "refuted-predicate-induction: ")
                    << declaration.name << '\n';
        output_ << "predicate: " << predicate.name << '\n';
        if (verified)
            output_ << "constructor-branches: " << predicate.constructors.size() << " verified\n";
        else
            output_ << "failed-constructor: " << failed_branch << '\n';
        output_ << "derivation-witness: erased after compiler-owned branch construction\n";
        return declaration.reusable && !verified ? 1 : 0;
    }

    int Runtime::execute_predicate_invariant(syntax::CheckDecl const &declaration) {
        if (declaration.assumes.size() != 1)
            reject(declaration.span, "a predicate invariant needs exactly one assumed predicate atom");
        syntax::Expr const &source_membership = declaration.assumes.front();
        if (source_membership.kind != syntax::Expr::Kind::call || !predicates_.contains(source_membership.name))
            reject(source_membership.span, "the invariant assumption must be a direct predicate call");
        if (!predicates_.at(source_membership.name)->horn_complete)
            reject(source_membership.span, "fixedpoint invariant checking is unavailable because `" +
                                               source_membership.name +
                                               "` has an arbitrary-fresh constructor retained outside Horn lowering");

        std::function<void(syntax::Expr const &)> reject_predicate_in_guarantee = [&](syntax::Expr const &expression) {
            if (expression.kind == syntax::Expr::Kind::call && predicates_.contains(expression.name))
                reject(expression.span, "predicate atoms are not yet admitted inside invariant guarantees");
            for (syntax::Expr const &child : expression.elements)
                reject_predicate_in_guarantee(child);
        };
        for (syntax::Expr const &condition : declaration.ensures)
            reject_predicate_in_guarantee(condition);

        ExpressionEnvironment environment;
        z3::expr_vector formal_parameters(context_);
        std::set<std::string> parameter_names;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const &parameter = declaration.parameters[i];
            if (!parameter_names.insert(parameter.name).second)
                reject(parameter.span, "duplicate check parameter `" + parameter.name + "`");
            TypePtr type = resolve_type(parameter.type);
            if (type->kind == RuntimeType::Kind::table)
                reject(parameter.type.span, "predicate invariant parameters must be native Fine values, not Table");
            std::string internal = "Fine.predicate-check." + declaration.name + ".arg" + std::to_string(i);
            z3::expr term = context_.constant(internal.c_str(), type->sort);
            environment.emplace(parameter.name, TypedExpression{type, term});
            formal_parameters.push_back(term);
        }

        std::string run_scope = "predicate-check:" + declaration.name;
        capture_source_edges_ = true;
        source_edge_within_ = {run_scope};
        TypedExpression membership = elaborate_expression(source_membership, environment);
        z3::expr guarantees = context_.bool_val(true);
        for (syntax::Expr const &condition : declaration.ensures) {
            TypedExpression elaborated = elaborate_expression(condition, environment);
            if (!same(elaborated.type, bool_type_))
                reject(condition.span, "predicate invariant guarantee must have type Bool");
            guarantees = guarantees && elaborated.expression;
        }
        capture_source_edges_ = false;
        source_edge_within_.clear();

        std::string counterexample_name = "Fine.predicate-check." + declaration.name + ".counterexample";
        z3::func_decl counterexample = context_.function(counterexample_name.c_str(), 0, nullptr, context_.bool_sort());
        fixedpoint_.register_relation(counterexample);
        z3::expr query = counterexample();
        z3::expr counterexample_body = membership.expression && !guarantees;
        z3::expr counterexample_rule = z3::implies(counterexample_body, query);
        counterexample_rule = z3::forall(formal_parameters, counterexample_rule);
        std::string rule_name = declaration.name + ".counterexample";
        fixedpoint_.add_rule(counterexample_rule, context_.str_symbol(rule_name.c_str()));

        std::unique_ptr<RainfallFixedpointObserver> fixedpoint_observer;
        if (rainfall_) {
            rainfall_->source_term(declaration.node_id, declaration.span, "decl.check", counterexample_rule,
                                   "generated", {run_scope});
            rainfall_->record("scope", "predicate-check.run.open", {run_scope}, "fine.fixedpoint",
                              "One counterexample-reachability query over a least predicate relation; exposes "
                              "compiler translation, public Spacer callbacks, and the public answer, not rule matches "
                              "or a source derivation witness",
                              {RainfallRecorder::string_field("declaration", declaration.name),
                               RainfallRecorder::string_field("predicate", source_membership.name),
                               RainfallRecorder::number_field("parameters", declaration.parameters.size()),
                               RainfallRecorder::boolean_field("ground", false)});
            rainfall_->record(
                "transform", "predicate-check.invariant.translate", {run_scope}, "fine.elaborator",
                "Compiler-owned negated-guarantee reachability rule over the least predicate relation",
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
            fixedpoint_observer = std::make_unique<RainfallFixedpointObserver>(fixedpoint_, *rainfall_,
                                                                               std::vector<std::string>{run_scope});
        }

        z3::check_result result = fixedpoint_.query(query);
        if (fixedpoint_observer)
            fixedpoint_observer->rethrow_if_failed();
        if (result == z3::unknown)
            reject(declaration.span, "predicate invariant query was unknown: " + fixedpoint_.reason_unknown());
        bool verified = result == z3::unsat;
        z3::expr answer = fixedpoint_.get_answer();
        if (rainfall_) {
            rainfall_->record("transition", "solver.fixedpoint.result", {run_scope}, "z3.public-api",
                              "Final public fixedpoint result and answer; retained callback lemmas are not claimed to "
                              "cause this result",
                              {RainfallRecorder::string_field("query", rainfall_->term(query)),
                               RainfallRecorder::string_field("answer", rainfall_->term(answer)),
                               RainfallRecorder::string_field("status", verified ? "unsat" : "sat"),
                               RainfallRecorder::string_field("polarity", "counterexample-reachable"),
                               RainfallRecorder::string_field("domain_outcome", verified ? "verified" : "refuted")});
            rainfall_->validate_terms();
            rainfall_->record("scope", "predicate-check.run.close", {run_scope}, "fine.fixedpoint",
                              "Open predicate invariant check completed",
                              {RainfallRecorder::string_field("status", verified ? "verified" : "refuted")});
        }

        output_ << (verified ? "verified-predicate-invariant: " : "refuted-predicate-invariant: ") << declaration.name
                << '\n';
        output_ << "predicate: " << source_membership.name << '\n';
        output_ << (verified ? "counterexample: none\n"
                             : "counterexample: fixedpoint reachability only (answer retained by rainfall)\n");
        return 0;
    }

}  // namespace fine::runtime_detail
