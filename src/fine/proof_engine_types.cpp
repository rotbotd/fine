#include "elaboration_internal.h"

// Proof-type elaboration and exact index matching. This unit knows how static
// indices are interpreted; it does not enumerate or select proof terms.
namespace fine::elaboration {

    IdentityType ProofEngine::elaborate_identity(syntax::ProofType const &type, ValueEnvironment const &values,
                                                 ProofEnvironment const &proofs,
                                                 std::vector<std::string> const &proof_order,
                                                 std::vector<z3::expr> const &absorbed) {
        if (type.kind != syntax::ProofType::Kind::identity)
            reject(type.span, "expected identity proof type, found `" + print_proof_type(type) + "`");
        values_.require_known_type(type.carrier);
        ValueKind carrier = kind_of(type.carrier);
        ValueTerm left = values_.elaborate_value(type.left, values, proofs, proof_order, absorbed, carrier);
        ValueTerm right = values_.elaborate_value(type.right, values, proofs, proof_order, absorbed, carrier);
        if (left.kind != carrier || right.kind != carrier)
            reject(type.span, "identity endpoints do not have carrier type `" + std::string(kind_name(carrier)) + "`");
        return {carrier, std::move(left.expression), std::move(right.expression)};
    }
    InductiveType ProofEngine::elaborate_inductive_type(syntax::ProofType const &type, ValueEnvironment const &values,
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
            values_.require_known_type(family.indices[i].type);
            ValueKind expected = kind_of(family.indices[i].type);
            ValueTerm index =
                values_.elaborate_value(type.arguments[i], values, proofs, proof_order, absorbed, expected);
            if (index.kind != expected)
                reject(type.arguments[i].span,
                       "index `" + family.indices[i].name + "` of `" + type.name + "` has the wrong value type");
            result.indices.push_back(std::move(index));
        }
        return result;
    }
    SemanticProofType ProofEngine::elaborate_proof_type(syntax::ProofType const &type, ValueEnvironment const &values,
                                                        ProofEnvironment const &proofs,
                                                        std::vector<std::string> const &proof_order,
                                                        std::vector<z3::expr> const &absorbed) {
        if (type.kind == syntax::ProofType::Kind::identity)
            return elaborate_identity(type, values, proofs, proof_order, absorbed);
        return elaborate_inductive_type(type, values, proofs, proof_order, absorbed);
    }
    bool ProofEngine::is_refinable_index(syntax::ValueExpr const &source, ValueTerm const &term) {
        if (source.kind != syntax::ValueExpr::Kind::name || !term.expression.is_app() ||
            term.expression.num_args() != 0 || term.expression.decl().decl_kind() != Z3_OP_UNINTERPRETED)
            return false;
        return term.expression.decl().name().str().starts_with("fine.proof-function.");
    }
    bool ProofEngine::bind_result_index(syntax::ValueExpr const &pattern, z3::expr const &target,
                                        std::string const &target_source,
                                        std::map<std::string, ValueKind> const &parameters, ValueEnvironment &bindings,
                                        std::map<std::string, std::string> &source_bindings) {
        if (pattern.kind != syntax::ValueExpr::Kind::name || !parameters.contains(pattern.name))
            return true;
        ValueKind kind = parameters.at(pattern.name);
        if (!Z3_is_eq_sort(values_.context(), target.get_sort(), values_.sort(kind)))
            return false;
        if (auto found = bindings.find(pattern.name); found != bindings.end())
            return same_ast(values_.context(), found->second.expression, target);
        bindings.emplace(pattern.name, ValueTerm(kind, target));
        source_bindings.emplace(pattern.name, target_source);
        return true;
    }
    bool ProofEngine::value_pattern_ready(syntax::ValueExpr const &pattern,
                                          std::map<std::string, ValueKind> const &parameters,
                                          ValueEnvironment const &bindings) {
        if (pattern.kind == syntax::ValueExpr::Kind::name && parameters.contains(pattern.name))
            return bindings.contains(pattern.name);
        return std::all_of(pattern.elements.begin(), pattern.elements.end(), [&](syntax::ValueExpr const &element) {
            return value_pattern_ready(element, parameters, bindings);
        });
    }
    bool ProofEngine::match_index_pattern(syntax::ValueExpr const &pattern, z3::expr const &target,
                                          std::string const &target_source,
                                          std::map<std::string, ValueKind> const &parameters,
                                          ValueEnvironment &bindings,
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
        ValueTerm instantiated = values_.elaborate_value(pattern, bindings, no_proofs, no_proof_order, no_absorbed);
        return same_ast(values_.context(), instantiated.expression, target);
    }
    bool ProofEngine::match_identity_pattern(syntax::ProofType const &pattern, ProofEvidence const &target,
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
    bool ProofEngine::complete_index_instantiation(syntax::ProofFunctionDecl const &function,
                                                   IdentityType const &expected,
                                                   IndexInstantiation const &instantiation) {
        if (instantiation.values.size() != function.parameters.size())
            return false;
        ProofEnvironment no_proofs;
        std::vector<std::string> no_proof_order;
        std::vector<z3::expr> no_absorbed;
        IdentityType result =
            elaborate_identity(function.result_type, instantiation.values, no_proofs, no_proof_order, no_absorbed);
        return same_type(values_.context(), result, expected);
    }
    std::string ProofEngine::index_instantiation_key(syntax::ProofFunctionDecl const &function,
                                                     IndexInstantiation const &instantiation) {
        std::ostringstream key;
        for (auto const &parameter : function.parameters) {
            key << parameter.name << '=';
            if (auto found = instantiation.values.find(parameter.name); found != instantiation.values.end())
                key << Z3_get_ast_id(values_.context(), found->second.expression);
            key << ';';
        }
        return key.str();
    }
    std::vector<IndexInstantiation>
    ProofEngine::infer_value_arguments(syntax::ProofFunctionDecl const &function, IdentityType const &expected,
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
    ProofEngine::infer_inductive_value_arguments(syntax::ProofFunctionDecl const &function,
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
        if (!same_type(values_.context(), instantiated, SemanticProofType(expected)))
            return std::nullopt;
        return result;
    }

}  // namespace fine::elaboration
