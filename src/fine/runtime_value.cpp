#include "runtime_internal.h"

// Runtime-value semantics: native enum datatypes, value expressions, proof-type
// index elaboration, and exact index unification shared by proof search.
namespace fine::runtime_detail {

    void Elaborator::request_materialization(syntax::ConcreteRange range, std::string text, syntax::SourceSpan span) {
        auto [found, inserted] = materializations_.emplace(std::pair{range.begin, range.end}, text);
        if (!inserted && found->second != text)
            reject(span, "two materializations disagree at one source range");
    }
    z3::sort Elaborator::sort(ValueKind const &kind) {
        if (kind.tag == ValueKind::Tag::integer)
            return context_.int_sort();
        if (kind.tag == ValueKind::Tag::boolean)
            return context_.bool_sort();
        auto found = enums_.find(kind.name);
        if (found == enums_.end())
            throw std::logic_error("unknown runtime enum kind `" + kind.name + "`");
        return found->second->sort;
    }
    void Elaborator::require_known_type(syntax::ValueType const &type) {
        if (type.kind == syntax::ValueType::Kind::enumeration && !enums_.contains(type.name))
            reject(type.span, "unknown value type `" + type.name + "`");
    }
    void Elaborator::declare_enum(syntax::EnumDecl const &declaration) {
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
    void Elaborator::record_boundary() {
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
    void Elaborator::ensure_fresh(std::string const &name, syntax::SourceSpan span, ValueEnvironment const &values,
                                  ProofEnvironment const &proofs) {
        if (values.contains(name) || proofs.contains(name))
            reject(span, "duplicate local name `" + name + "`");
    }
    ValueTerm Elaborator::elaborate_constructor(syntax::ValueExpr const &expression, RuntimeEnum &enumeration,
                                                RuntimeConstructor const &constructor, ValueEnvironment const &values,
                                                ProofEnvironment const &proofs,
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
    ValueTerm Elaborator::elaborate_match(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                          ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
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
    ValueTerm Elaborator::elaborate_value(syntax::ValueExpr const &expression, ValueEnvironment const &values,
                                          ProofEnvironment const &proofs, std::vector<std::string> const &proof_order,
                                          std::vector<z3::expr> const &absorbed) {
        switch (expression.kind) {
        case syntax::ValueExpr::Kind::name: {
            auto value = values.find(expression.name);
            if (value != values.end())
                return value->second;
            if (proofs.contains(expression.name))
                reject(expression.span, "proof `" + expression.name + "` cannot inhabit a runtime value");
            if (proof_constructors_.contains(expression.name))
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
    IdentityType Elaborator::elaborate_identity(syntax::ProofType const &type, ValueEnvironment const &values,
                                                ProofEnvironment const &proofs,
                                                std::vector<std::string> const &proof_order,
                                                std::vector<z3::expr> const &absorbed) {
        if (type.kind != syntax::ProofType::Kind::identity)
            reject(type.span, "expected identity proof type, found `" + print_proof_type(type) + "`");
        require_known_type(type.carrier);
        ValueKind carrier = kind_of(type.carrier);
        ValueTerm left = elaborate_value(type.left, values, proofs, proof_order, absorbed);
        ValueTerm right = elaborate_value(type.right, values, proofs, proof_order, absorbed);
        if (left.kind != carrier || right.kind != carrier)
            reject(type.span, "identity endpoints do not have carrier type `" + std::string(kind_name(carrier)) + "`");
        return {carrier, std::move(left.expression), std::move(right.expression)};
    }
    InductiveType Elaborator::elaborate_inductive_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                                       ProofEnvironment const &proofs,
                                                       std::vector<std::string> const &proof_order,
                                                       std::vector<z3::expr> const &absorbed) {
        auto found = proof_inductives_.find(type.name);
        if (found == proof_inductives_.end())
            reject(type.span, "unknown proof inductive `" + type.name + "`");
        auto const &family = *found->second;
        if (type.arguments.size() != family.indices.size())
            reject(type.span,
                   "proof inductive `" + type.name + "` expects " + std::to_string(family.indices.size()) + " indices");
        InductiveType result{type.name, {}};
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            ValueTerm index = elaborate_value(type.arguments[i], values, proofs, proof_order, absorbed);
            require_known_type(family.indices[i].type);
            if (index.kind != kind_of(family.indices[i].type))
                reject(type.arguments[i].span,
                       "index `" + family.indices[i].name + "` of `" + type.name + "` has the wrong value type");
            result.indices.push_back(std::move(index));
        }
        return result;
    }
    SemanticProofType Elaborator::elaborate_proof_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                                       ProofEnvironment const &proofs,
                                                       std::vector<std::string> const &proof_order,
                                                       std::vector<z3::expr> const &absorbed) {
        if (type.kind == syntax::ProofType::Kind::identity)
            return elaborate_identity(type, values, proofs, proof_order, absorbed);
        return elaborate_inductive_type(type, values, proofs, proof_order, absorbed);
    }
    bool Elaborator::contains_parameter(syntax::ValueExpr const &expression,
                                        std::map<std::string, ValueKind> const &parameters,
                                        ValueEnvironment const &bindings) {
        if (expression.kind == syntax::ValueExpr::Kind::name && parameters.contains(expression.name) &&
            !bindings.contains(expression.name))
            return true;
        return std::any_of(
            expression.elements.begin(), expression.elements.end(),
            [&](syntax::ValueExpr const &element) { return contains_parameter(element, parameters, bindings); });
    }
    bool Elaborator::match_constructor_index(syntax::ValueExpr const &pattern, z3::expr const &target,
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
    bool Elaborator::is_refinable_index(syntax::ValueExpr const &source, ValueTerm const &term) {
        if (source.kind != syntax::ValueExpr::Kind::name || !term.expression.is_app() ||
            term.expression.num_args() != 0 || term.expression.decl().decl_kind() != Z3_OP_UNINTERPRETED)
            return false;
        return term.expression.decl().name().str().starts_with("fine.proof-function.");
    }
    bool Elaborator::bind_result_index(syntax::ValueExpr const &pattern, z3::expr const &target,
                                       std::string const &target_source,
                                       std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings,
                                       std::map<std::string, std::string> &source_bindings) {
        if (pattern.kind != syntax::ValueExpr::Kind::name || !parameters.contains(pattern.name))
            return true;
        ValueKind kind = parameters.at(pattern.name);
        if (!Z3_is_eq_sort(context_, target.get_sort(), sort(kind)))
            return false;
        if (auto found = bindings.find(pattern.name); found != bindings.end())
            return same_ast(context_, found->second.expression, target);
        bindings.emplace(pattern.name, ValueTerm(kind, target));
        source_bindings.emplace(pattern.name, target_source);
        return true;
    }
    bool Elaborator::value_pattern_ready(syntax::ValueExpr const &pattern,
                                         std::map<std::string, ValueKind> const &parameters,
                                         ValueEnvironment const &bindings) {
        if (pattern.kind == syntax::ValueExpr::Kind::name && parameters.contains(pattern.name))
            return bindings.contains(pattern.name);
        return std::all_of(pattern.elements.begin(), pattern.elements.end(), [&](syntax::ValueExpr const &element) {
            return value_pattern_ready(element, parameters, bindings);
        });
    }
    bool Elaborator::match_index_pattern(syntax::ValueExpr const &pattern, z3::expr const &target,
                                         std::string const &target_source,
                                         std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings,
                                         std::map<std::string, std::string> &source_bindings, bool &added) {
        std::size_t before = bindings.size();
        if (!bind_result_index(pattern, target, target_source, parameters, bindings, source_bindings))
            return false;
        added = added || bindings.size() != before;
        if (!value_pattern_ready(pattern, parameters, bindings))
            return true;
        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        ValueTerm instantiated = elaborate_value(pattern, bindings, no_proofs, no_proof_order, no_absorbed);
        return same_ast(context_, instantiated.expression, target);
    }
    bool Elaborator::match_identity_pattern(syntax::ProofType const &pattern, ProofEvidence const &target,
                                            std::map<std::string, ValueKind> const &parameters,
                                            ValueEnvironment &bindings,
                                            std::map<std::string, std::string> &source_bindings, bool &added) {
        auto identity = std::get_if<IdentityType>(&target.type);
        if (!identity)
            return false;
        return kind_of(pattern.carrier) == identity->carrier &&
               match_index_pattern(pattern.left, identity->left, target.left_source, parameters, bindings,
                                   source_bindings, added) &&
               match_index_pattern(pattern.right, identity->right, target.right_source, parameters, bindings,
                                   source_bindings, added);
    }
    bool Elaborator::complete_index_instantiation(syntax::ProofFunctionDecl const &function,
                                                  IdentityType const &expected,
                                                  IndexInstantiation const &instantiation) {
        if (instantiation.values.size() != function.parameters.size())
            return false;
        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        IdentityType result =
            elaborate_identity(function.result_type, instantiation.values, no_proofs, no_proof_order, no_absorbed);
        return same_type(context_, result, expected);
    }
    std::string Elaborator::index_instantiation_key(syntax::ProofFunctionDecl const &function,
                                                    IndexInstantiation const &instantiation) {
        std::ostringstream key;
        for (auto const &parameter : function.parameters) {
            key << parameter.name << '=';
            if (auto found = instantiation.values.find(parameter.name); found != instantiation.values.end())
                key << Z3_get_ast_id(context_, found->second.expression);
            key << ';';
        }
        return key.str();
    }
    std::vector<IndexInstantiation>
    Elaborator::infer_value_arguments(syntax::ProofFunctionDecl const &function, IdentityType const &expected,
                                      std::string const &left_source, std::string const &right_source,
                                      ProofEnvironment const &proofs, std::vector<std::string> const &proof_order) {
        if (kind_of(function.result_type.carrier) != expected.carrier)
            return {};
        std::map<std::string, ValueKind> parameters;
        for (auto const &parameter : function.parameters)
            parameters.emplace(parameter.name, kind_of(parameter.type));
        IndexInstantiation initial;
        if (!bind_result_index(function.result_type.left, expected.left, left_source, parameters, initial.values,
                               initial.sources) ||
            !bind_result_index(function.result_type.right, expected.right, right_source, parameters, initial.values,
                               initial.sources))
            return {};

        std::vector<IndexInstantiation> completed;
        std::set<std::string> visited;
        auto search = [&](auto &&self, IndexInstantiation instantiation) -> void {
            std::string key = index_instantiation_key(function, instantiation);
            if (!visited.insert(key).second)
                return;
            if (complete_index_instantiation(function, expected, instantiation)) {
                completed.push_back(std::move(instantiation));
                return;
            }
            for (auto const &parameter : function.proof_parameters) {
                for (auto const &proof_name : proof_order) {
                    auto found = proofs.find(proof_name);
                    if (found == proofs.end())
                        continue;
                    IndexInstantiation extended = instantiation;
                    bool added = false;
                    if (match_identity_pattern(parameter.type, found->second, parameters, extended.values,
                                               extended.sources, added) &&
                        added)
                        self(self, std::move(extended));
                }
            }
        };
        search(search, std::move(initial));
        return completed;
    }
    std::optional<IndexInstantiation>
    Elaborator::infer_inductive_value_arguments(syntax::ProofFunctionDecl const &function,
                                                syntax::ProofType const &expected_syntax,
                                                InductiveType const &expected) {
        if (function.result_type.kind != syntax::ProofType::Kind::inductive ||
            function.result_type.name != expected.family ||
            function.result_type.arguments.size() != expected.indices.size() ||
            expected_syntax.arguments.size() != expected.indices.size())
            return std::nullopt;
        std::map<std::string, ValueKind> parameters;
        for (auto const &parameter : function.parameters)
            parameters.emplace(parameter.name, kind_of(parameter.type));
        IndexInstantiation result;
        bool added = false;
        for (std::size_t i = 0; i < expected.indices.size(); ++i)
            if (!match_index_pattern(function.result_type.arguments[i], expected.indices[i].expression,
                                     print_value(expected_syntax.arguments[i]), parameters, result.values,
                                     result.sources, added))
                return std::nullopt;
        if (result.values.size() != function.parameters.size())
            return std::nullopt;
        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        SemanticProofType instantiated =
            elaborate_proof_type(function.result_type, result.values, no_proofs, no_proof_order, no_absorbed);
        if (!same_type(context_, instantiated, SemanticProofType(expected)))
            return std::nullopt;
        return result;
    }

}  // namespace fine::runtime_detail
