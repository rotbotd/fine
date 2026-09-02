#include "runtime_internal.h"

namespace fine::runtime_detail {

    void Runtime::declare_proof_family(syntax::ProofFamilyDecl const &declaration) {
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
            [](syntax::ProofConstructor const &constructor) { return !constructor.arbitrary_premises.empty(); });
        info->relation = context_.function(declaration.name.c_str(), static_cast<unsigned>(index_sorts.size()),
                                           index_sorts.data(), context_.bool_sort());
        if (info->horn_complete)
            fixedpoint_.register_relation(info->relation);
        ProofFamilyInfo *stable = info.get();
        proof_families_.emplace(declaration.name, std::move(info));

        std::set<std::string> constructor_names;
        std::string family_scope = "proof-family:" + declaration.name;
        if (rainfall_)
            rainfall_->record("object", "fine.proof-family.relation", {family_scope}, "fine.elaborator",
                              stable->horn_complete
                                  ? "Erased indexed proposition represented by a native-sort least relation"
                                  : "Erased indexed proposition represented for induction by a compiler-owned "
                                    "constructor table and relation-shaped term handle; no constructor is registered "
                                    "with fixedpoint because one has an arbitrary field",
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
                    reject(parameter.type.span, "proof-constructor parameters must be native Fine values, not Table");
                std::string internal =
                    "Fine.proof." + declaration.name + "." + constructor.name + ".arg" + std::to_string(i);
                z3::expr term = context_.constant(internal.c_str(), type->sort);
                environment.emplace(parameter.name, TypedExpression{type, term});
                formal_parameters.push_back(term);
                retained_constructor.parameters.push_back(term);
            }

            auto atom = [&](syntax::Expr const &expression, std::string_view role) {
                if (expression.kind != syntax::Expr::Kind::call || !proof_families_.contains(expression.name))
                    reject(expression.span, std::string(role) + " must be a direct call to a declared proof family");
                TypedExpression elaborated = elaborate_expression(expression, environment);
                if (!same(elaborated.type, bool_type_))
                    reject(expression.span, std::string(role) + " must be an indexed proposition");
                return elaborated.expression;
            };

            z3::expr conclusion = atom(constructor.result, "proof-constructor result");
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

            for (std::size_t field_ordinal = 0; field_ordinal < constructor.arbitrary_premises.size();
                 ++field_ordinal) {
                syntax::ArbitraryPremise const &field = constructor.arbitrary_premises[field_ordinal];
                bool previous_capture = capture_source_edges_;
                std::vector<std::string> previous_within = source_edge_within_;
                capture_source_edges_ = true;
                source_edge_within_ = {family_scope};
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

                ProofConstructorInfo::ArbitraryField retained_field(context_);
                retained_field.binder = field.binder;
                retained_field.view_name = field.view_name;
                retained_field.binder_term =
                    context_.constant(("Fine.proof." + declaration.name + "." + constructor.name + ".arbitrary" +
                                       std::to_string(field_ordinal))
                                          .c_str(),
                                      view.carrier->sort);
                if (rainfall_)
                    rainfall_->source_term(field.node_id, field.span, "proof.arbitrary-field",
                                           retained_field.binder_term, "generated", {family_scope});

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
                            !proof_families_.contains(scoped_premise.name))
                            reject(scoped_premise.span, "an arbitrary-fresh premise must call a declared proof family");
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
                        "derive", "fine.proof-constructor.arbitrary-field", {family_scope}, "fine.elaborator",
                        "Compiler-owned arbitrary-fresh proof field; its view requirement and recursive premise are "
                        "retained and are deliberately not inserted into a Horn body",
                        {RainfallRecorder::string_field("family", declaration.name),
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
                        "derive",
                        stable->horn_complete ? "fine.proof-constructor.rule" : "fine.proof-constructor.branch",
                        {family_scope}, "fine.elaborator",
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

    int Runtime::execute_proof_family_check(syntax::CheckDecl const &declaration) {
        reserve_value_name(declaration.name, declaration.span);
        if (declaration.induction_parameter)
            reject(*declaration.induction_span, "proof-family membership does not yet implement derivation induction");
        if (declaration.proof_induction)
            return execute_proof_family_induction(declaration);
        if (!declaration.parameters.empty())
            return execute_proof_family_invariant(declaration);
        if (!declaration.assumes.empty())
            reject(declaration.span, "a least-relation membership check cannot yet mix ordinary assumptions");
        if (declaration.ensures.size() != 1)
            reject(declaration.span, "a least-relation membership check needs exactly one ensured atom");
        syntax::Expr const &source_query = declaration.ensures.front();
        if (source_query.kind != syntax::Expr::Kind::call || !proof_families_.contains(source_query.name))
            reject(source_query.span, "the ensured condition must be a direct proof-family call");
        if (!proof_families_.at(source_query.name)->horn_complete)
            reject(source_query.span, "least-relation membership is unavailable because `" + source_query.name +
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
            rainfall_->record("scope", "proof-check.run.open", {run_scope}, "fine.fixedpoint",
                              "One public least-relation membership query; records admitted constructor rules, public "
                              "Spacer callback boundaries, and the public result",
                              {RainfallRecorder::string_field("declaration", declaration.name),
                               RainfallRecorder::string_field("family", source_query.name),
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
            rainfall_->record("scope", "proof-check.run.close", {run_scope}, "fine.fixedpoint",
                              "Ground least-relation membership completed",
                              {RainfallRecorder::string_field("status", derived ? "derived" : "not-derived")});
        }
        output_ << (derived ? "derived: " : "not-derived: ") << declaration.name << '\n';
        output_ << "proof-family: " << source_query.name << '\n';
        output_ << "proof-witness: erased\n";
        return 0;
    }

    int Runtime::execute_proof_family_induction(syntax::CheckDecl const &declaration) {
        syntax::Expr const &target = *declaration.proof_induction;
        if (!proof_families_.contains(target.name))
            reject(target.span, "`inducts` target must call a declared proof family");
        if (declaration.assumes.size() != 1)
            reject(declaration.span, "proof-family induction needs exactly its target atom in `assumes`");
        syntax::Expr const &assumed = declaration.assumes.front();
        if (assumed.kind != syntax::Expr::Kind::call || assumed.name != target.name)
            reject(assumed.span, "proof-family induction assumption must be the same family as `inducts`");

        ProofFamilyInfo const &family = *proof_families_.at(target.name);
        if (declaration.parameters.size() != family.index_types.size() ||
            target.elements.size() != declaration.parameters.size())
            reject(target.span, "the first proof-induction slice needs exactly one check parameter per family index");

        ExpressionEnvironment environment;
        z3::expr_vector check_terms(context_);
        std::set<std::string> parameter_names;
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            syntax::Parameter const &parameter = declaration.parameters[i];
            if (!parameter_names.insert(parameter.name).second)
                reject(parameter.span, "duplicate check parameter `" + parameter.name + "`");
            TypePtr type = resolve_type(parameter.type);
            if (!same(type, family.index_types[i]))
                reject(parameter.type.span, "proof-induction parameter " + std::to_string(i + 1) + " must have type `" +
                                                family.index_types[i]->display + "`");
            if (target.elements[i].kind != syntax::Expr::Kind::name || target.elements[i].name != parameter.name)
                reject(target.elements[i].span,
                       "proof-induction indices must be the check parameters in declaration order");
            std::string internal = "Fine.proof-induction." + declaration.name + ".arg" + std::to_string(i);
            z3::expr term = context_.constant(internal.c_str(), type->sort);
            environment.emplace(parameter.name, TypedExpression{type, term});
            check_terms.push_back(term);
        }

        std::function<void(syntax::Expr const &)> reject_family_in_guarantee = [&](syntax::Expr const &expression) {
            if (expression.kind == syntax::Expr::Kind::call && proof_families_.contains(expression.name))
                reject(expression.span, "proof-family atoms are not yet admitted inside induction guarantees");
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
                "Fine-owned structural induction over proof-family constructors; each recursive family premise becomes "
                "one exact induction hypothesis and each branch is checked by a separate public SMT query",
                {RainfallRecorder::string_field("declaration", declaration.name),
                 RainfallRecorder::string_field("family", family.name),
                 RainfallRecorder::number_field("constructors", family.constructors.size()),
                 RainfallRecorder::string_field("target", rainfall_->term(target_term.expression)),
                 RainfallRecorder::string_field("guarantees", rainfall_->term(guarantees))});

        auto instantiate = [&](z3::expr const &expression, std::vector<z3::expr> const &indices) {
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
                reject(target.span, "proof induction currently requires every constructor premise to recurse on `" +
                                        family.name + "`");
            std::string branch_scope = "branch:" + constructor.name;
            z3::expr constructor_result = family.relation(static_cast<unsigned>(constructor.result_indices.size()),
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
                        "derive", "proof-induction.hypothesis", {run_scope, branch_scope}, "fine.induction",
                        "Compiler-owned recursive constructor premise paired with the guarantee instantiated at its "
                        "exact indices",
                        {RainfallRecorder::string_field("constructor", constructor.name),
                         RainfallRecorder::number_field("premise_ordinal", i),
                         RainfallRecorder::string_field("recursive_premise", rainfall_->term(premise)),
                         RainfallRecorder::string_field("induction_hypothesis", rainfall_->term(hypothesis))});
                }
            }

            for (std::size_t field_ordinal = 0; field_ordinal < constructor.arbitrary_fields.size(); ++field_ordinal) {
                ProofConstructorInfo::ArbitraryField const &field = constructor.arbitrary_fields[field_ordinal];

                z3::expr availability = field.requirement;
                if (field.availability_witness) {
                    z3::expr_vector source(context_);
                    z3::expr_vector destination(context_);
                    source.push_back(field.binder_term);
                    destination.push_back(*field.availability_witness);
                    availability = availability.substitute(source, destination);
                }
                else {
                    z3::expr_vector binders(context_);
                    binders.push_back(field.binder_term);
                    availability = z3::exists(binders, availability);
                }
                if (!constructor.parameters.empty()) {
                    z3::expr_vector constructor_parameters(context_);
                    for (z3::expr const &parameter : constructor.parameters)
                        constructor_parameters.push_back(parameter);
                    availability = z3::forall(constructor_parameters, availability);
                }
                z3::solver availability_solver(context_);
                for (AdmittedLemma const &lemma : admitted_lemmas_) {
                    availability_solver.add(lemma.theorem);
                    if (rainfall_)
                        rainfall_->record(
                            "constraint", "lemma.use", {run_scope, branch_scope}, "fine.induction",
                            "Previously verified source lemma admitted to the arbitrary-field availability query",
                            {RainfallRecorder::string_field("lemma", lemma.name),
                             RainfallRecorder::string_field("theorem", rainfall_->term(lemma.theorem)),
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
                        "transition", "proof-induction.arbitrary.availability", {run_scope, branch_scope},
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

                hypotheses = hypotheses && field.requirement;
                for (std::size_t i = 0; i < field.recursive_premise_indices.size(); ++i) {
                    std::vector<z3::expr> const &indices = field.recursive_premise_indices[i];
                    z3::expr hypothesis = instantiate(guarantees, indices);
                    hypotheses = hypotheses && hypothesis;
                    hypothesis_terms.push_back(hypothesis);
                    if (rainfall_)
                        rainfall_->record(
                            "derive", "proof-induction.arbitrary-hypothesis", {run_scope, branch_scope},
                            "fine.induction",
                            "Exact recursive premise and induction hypothesis under one scoped arbitrary-fresh carrier "
                            "value and its independently retained view requirement",
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
                             RainfallRecorder::string_field("scope_owner", "fine-arbitrary-field")});
                }
            }
            z3::expr branch_query = hypotheses && !branch_goal;
            if (rainfall_) {
                rainfall_->source_term(declaration.node_id, declaration.span, "decl.check", branch_query, "generated",
                                       {run_scope, branch_scope});
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
            for (AdmittedLemma const &lemma : admitted_lemmas_) {
                solver.add(lemma.theorem);
                if (rainfall_)
                    rainfall_->record(
                        "constraint", "lemma.use", {run_scope, branch_scope}, "fine.induction",
                        "Previously verified source lemma admitted to this constructor branch as a universal SMT "
                        "assumption",
                        {RainfallRecorder::string_field("lemma", lemma.name),
                         RainfallRecorder::string_field("theorem", rainfall_->term(lemma.theorem)),
                         RainfallRecorder::string_field("consumer", declaration.name),
                         RainfallRecorder::string_field("constructor", constructor.name)});
            }
            solver.add(branch_query);
            z3::check_result result = solver.check();
            if (result == z3::unknown)
                reject(target.span,
                       "proof-induction branch `" + constructor.name + "` was unknown: " + solver.reason_unknown());
            bool branch_verified = result == z3::unsat;
            if (rainfall_) {
                rainfall_->record(
                    "transition", "proof-induction.branch.result", {run_scope, branch_scope}, "z3.public-api",
                    "Final public SMT result for one compiler-generated constructor branch",
                    {RainfallRecorder::string_field("constructor", constructor.name),
                     RainfallRecorder::string_field("status", branch_verified ? "unsat" : "sat"),
                     RainfallRecorder::string_field("domain_outcome", branch_verified ? "verified" : "refuted")});
                rainfall_->record("scope", "proof-induction.branch.close", {run_scope, branch_scope}, "fine.induction",
                                  "Constructor induction branch completed",
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
            rainfall_->record("scope", "proof-induction.run.close", {run_scope}, "fine.induction",
                              "Compiler-generated proof-family induction completed",
                              {RainfallRecorder::string_field("status", verified ? "verified" : "refuted"),
                               RainfallRecorder::string_field("failed_constructor", failed_branch)});
        }
        output_ << (verified ? "verified-proof-induction: " : "refuted-proof-induction: ") << declaration.name << '\n';
        output_ << "proof-family: " << family.name << '\n';
        if (verified)
            output_ << "constructor-branches: " << family.constructors.size() << " verified\n";
        else
            output_ << "failed-constructor: " << failed_branch << '\n';
        output_ << "proof-witness: erased after compiler-owned branch construction\n";
        return 0;
    }

    int Runtime::execute_proof_family_invariant(syntax::CheckDecl const &declaration) {
        if (declaration.assumes.size() != 1)
            reject(declaration.span, "a proof-family invariant needs exactly one assumed family atom");
        syntax::Expr const &source_membership = declaration.assumes.front();
        if (source_membership.kind != syntax::Expr::Kind::call || !proof_families_.contains(source_membership.name))
            reject(source_membership.span, "the invariant assumption must be a direct proof-family call");
        if (!proof_families_.at(source_membership.name)->horn_complete)
            reject(source_membership.span, "fixedpoint invariant checking is unavailable because `" +
                                               source_membership.name +
                                               "` has an arbitrary-fresh constructor retained outside Horn lowering");

        std::function<void(syntax::Expr const &)> reject_family_in_guarantee = [&](syntax::Expr const &expression) {
            if (expression.kind == syntax::Expr::Kind::call && proof_families_.contains(expression.name))
                reject(expression.span, "proof-family atoms are not yet admitted inside invariant guarantees");
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
                reject(parameter.type.span, "proof-family invariant parameters must be native Fine values, not Table");
            std::string internal = "Fine.proof-check." + declaration.name + ".arg" + std::to_string(i);
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
            rainfall_->record("scope", "proof-check.run.open", {run_scope}, "fine.fixedpoint",
                              "One counterexample-reachability query over a least proof-family relation; exposes "
                              "compiler translation, public Spacer callbacks, and the public answer, not rule matches "
                              "or a source proof witness",
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
            fixedpoint_observer = std::make_unique<RainfallFixedpointObserver>(fixedpoint_, *rainfall_,
                                                                               std::vector<std::string>{run_scope});
        }

        z3::check_result result = fixedpoint_.query(query);
        if (fixedpoint_observer)
            fixedpoint_observer->rethrow_if_failed();
        if (result == z3::unknown)
            reject(declaration.span, "proof-family invariant query was unknown: " + fixedpoint_.reason_unknown());
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
            rainfall_->record("scope", "proof-check.run.close", {run_scope}, "fine.fixedpoint",
                              "Open proof-family invariant check completed",
                              {RainfallRecorder::string_field("status", verified ? "verified" : "refuted")});
        }

        output_ << (verified ? "verified-family-invariant: " : "refuted-family-invariant: ") << declaration.name
                << '\n';
        output_ << "proof-family: " << source_membership.name << '\n';
        output_ << (verified ? "counterexample: none\n"
                             : "counterexample: fixedpoint reachability only (answer retained by rainfall)\n");
        return 0;
    }

}  // namespace fine::runtime_detail
