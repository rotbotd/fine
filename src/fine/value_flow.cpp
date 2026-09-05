#include "value_flow.h"

#include "runtime.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fine::stage {

    namespace {

        std::string type_key(FlowType const &type) {
            switch (type.kind) {
            case syntax::ValueType::Kind::integer: return "I";
            case syntax::ValueType::Kind::boolean: return "B";
            case syntax::ValueType::Kind::enumeration: return "E" + std::to_string(type.name.size()) + ':' + type.name;
            }
            throw std::logic_error("unknown Fine value type");
        }

        FlowType flow_type(syntax::ValueType const &type) {
            return {type.kind, type.kind == syntax::ValueType::Kind::enumeration ? type.name : std::string{}};
        }

        std::string field(std::string_view value) {
            return std::to_string(value.size()) + ':' + std::string(value);
        }

        struct ConstructorSignature {
            std::string enumeration;
            std::vector<FlowType> fields;
        };

        struct FunctionSignature {
            std::vector<FlowType> parameters;
            FlowType result;
        };

        struct ProofConstructorSignature {
            std::string family;
            std::vector<syntax::ValueParameter> parameters;
            std::size_t explicit_proofs = 0;
            std::vector<syntax::ValueExpr> result_indices;
        };

    }  // namespace

    bool CertifiedValueFlowProgram::recursion_certified(std::string const &function) const {
        auto found = program_.function_sccs().find(function);
        return found != program_.function_sccs().end() && certified_recursive_sccs_.contains(found->second);
    }

    CertifiedValueFlowProgram build_certified_value_flow(syntax::Document const &document,
                                                          ExecutionResult const &execution) {
        CertifiedValueFlowProgram result;
        result.program_ = build_value_flow(document);
        std::map<std::string, syntax::FunctionDecl const *> declarations;
        for (auto const &function : document.functions)
            declarations.emplace(function.name, &function);
        for (auto const &certificate : execution.value_recursion_certificates) {
            if (certificate.functions_.empty() ||
                certificate.functions_.size() != certificate.declarations_.size())
                throw std::logic_error("malformed value-recursion certificate");
            std::optional<std::size_t> scc;
            for (std::size_t i = 0; i < certificate.functions_.size(); ++i) {
                auto declaration = declarations.find(certificate.functions_[i]);
                auto flow_scc = result.program_.function_sccs().find(certificate.functions_[i]);
                if (declaration == declarations.end() || declaration->second != certificate.declarations_[i] ||
                    flow_scc == result.program_.function_sccs().end())
                    throw std::logic_error("value-recursion certificate does not belong to this parsed document");
                if (scc && *scc != flow_scc->second)
                    throw std::logic_error("value-recursion certificate no longer names one source SCC");
                scc = flow_scc->second;
            }
            std::vector<std::string> certified_names = certificate.functions_;
            std::sort(certified_names.begin(), certified_names.end());
            if (!scc || result.program_.sccs().at(*scc).functions != certified_names)
                throw std::logic_error("value-recursion certificate and value-flow SCC disagree");
            result.certified_recursive_sccs_.insert(*scc);
        }
        return result;
    }

    class ValueFlowBuilder {
    public:
        explicit ValueFlowBuilder(syntax::Document const &document) : document_(document) {
            for (auto const &enumeration : document.enums) {
                if (!enums_.insert(enumeration.name).second)
                    throw std::runtime_error("duplicate enum in value-flow input: " + enumeration.name);
                for (auto const &constructor : enumeration.constructors) {
                    ConstructorSignature signature;
                    signature.enumeration = enumeration.name;
                    for (auto const &type : constructor.fields)
                        signature.fields.push_back(flow_type(type));
                    if (!constructors_.emplace(constructor.name, std::move(signature)).second)
                        throw std::runtime_error("duplicate constructor in value-flow input: " + constructor.name);
                }
            }
            for (auto const &function : document.functions) {
                FunctionSignature signature;
                for (auto const &parameter : function.parameters)
                    signature.parameters.push_back(flow_type(parameter.type));
                signature.result = flow_type(function.result_type);
                if (!signatures_.emplace(function.name, std::move(signature)).second)
                    throw std::runtime_error("duplicate function in value-flow input: " + function.name);
            }
            for (auto const &family : document.proof_inductives)
                for (auto const &constructor : family.constructors)
                    proof_constructors_.emplace(constructor.name,
                                                ProofConstructorSignature{family.name, constructor.parameters,
                                                                          constructor.explicit_proof_parameters.size(),
                                                                          constructor.result_type.arguments});
        }

        ValueFlowProgram build() {
            ValueFlowProgram result;
            for (auto const &function : document_.functions) {
                ValueFlowFunction lowered = lower_function(function);
                result.functions_.emplace(lowered.name(), std::move(lowered));
            }
            build_sccs(result);
            return result;
        }

    private:
        syntax::Document const &document_;
        std::set<std::string> enums_;
        std::map<std::string, ConstructorSignature> constructors_;
        std::map<std::string, FunctionSignature> signatures_;
        std::map<std::string, ProofConstructorSignature> proof_constructors_;

        struct FunctionState {
            ValueFlowFunction function;
            std::map<std::string, std::pair<FlowLocalId, FlowType>> locals;
            std::map<std::string, syntax::ProofType const *> proof_locals;
            std::map<std::string, FlowNodeId> aliases;
            FlowLocalId next_local = 0;
        };

        FlowNodeId append(FunctionState &state, FlowNode node) {
            state.function.nodes_.push_back(std::move(node));
            return state.function.nodes_.size() - 1;
        }

        FlowNodeId lower(syntax::ValueExpr const &expression, FunctionState &state,
                         std::optional<FlowType> expected = std::nullopt) {
            if (expression.kind == syntax::ValueExpr::Kind::name) {
                if (auto alias = state.aliases.find(expression.name); alias != state.aliases.end())
                    return alias->second;
                if (auto local = state.locals.find(expression.name); local != state.locals.end())
                    return append(state, {FlowNode::Kind::local, local->second.second, {}, local->second.first});
                auto constructor = constructors_.find(expression.name);
                if (constructor == constructors_.end() || !constructor->second.fields.empty())
                    throw std::runtime_error("unresolved value-flow name: " + expression.name);
                FlowType type{syntax::ValueType::Kind::enumeration, constructor->second.enumeration};
                return append(state, {FlowNode::Kind::constructor, std::move(type), expression.name});
            }
            if (expression.kind == syntax::ValueExpr::Kind::integer)
                return append(
                    state, {FlowNode::Kind::integer, {syntax::ValueType::Kind::integer, {}}, expression.integer_text});
            if (expression.kind == syntax::ValueExpr::Kind::boolean)
                return append(state, {FlowNode::Kind::boolean,
                                      {syntax::ValueType::Kind::boolean, {}},
                                      expression.boolean_value ? "true" : "false"});
            if (expression.kind == syntax::ValueExpr::Kind::equal) {
                auto empty_match = [](syntax::ValueExpr const &value) {
                    return value.kind == syntax::ValueExpr::Kind::match && value.match_constructors.empty();
                };
                if (empty_match(expression.elements[0]) && empty_match(expression.elements[1]))
                    throw std::runtime_error("value-flow equality between empty matches has no operand type");
                std::vector<FlowNodeId> inputs(2);
                if (empty_match(expression.elements[0])) {
                    inputs[1] = lower(expression.elements[1], state);
                    inputs[0] = lower(expression.elements[0], state, state.function.nodes_[inputs[1]].type);
                }
                else {
                    inputs[0] = lower(expression.elements[0], state);
                    inputs[1] = lower(expression.elements[1], state, state.function.nodes_[inputs[0]].type);
                }
                if (inputs.size() != 2 ||
                    state.function.nodes_[inputs[0]].type != state.function.nodes_[inputs[1]].type)
                    throw std::runtime_error("ill-typed equality in value-flow input");
                return append(
                    state, {FlowNode::Kind::equal, {syntax::ValueType::Kind::boolean, {}}, {}, 0, std::move(inputs)});
            }
            if (expression.kind == syntax::ValueExpr::Kind::call) {
                if (auto constructor = constructors_.find(expression.name); constructor != constructors_.end()) {
                    if (expression.elements.size() != constructor->second.fields.size())
                        throw std::runtime_error("wrong constructor arity in value-flow input: " + expression.name);
                    std::vector<FlowNodeId> inputs;
                    for (std::size_t i = 0; i < expression.elements.size(); ++i)
                        inputs.push_back(lower(expression.elements[i], state, constructor->second.fields[i]));
                    for (std::size_t i = 0; i < inputs.size(); ++i)
                        if (state.function.nodes_[inputs[i]].type != constructor->second.fields[i])
                            throw std::runtime_error("wrong constructor field type in value-flow input: " +
                                                     expression.name);
                    FlowType type{syntax::ValueType::Kind::enumeration, constructor->second.enumeration};
                    return append(
                        state, {FlowNode::Kind::constructor, std::move(type), expression.name, 0, std::move(inputs)});
                }
                auto signature = signatures_.find(expression.name);
                if (signature == signatures_.end())
                    throw std::runtime_error("unresolved value-flow call: " + expression.name);
                if (expression.elements.size() != signature->second.parameters.size())
                    throw std::runtime_error("wrong function arity in value-flow input: " + expression.name);
                std::vector<FlowNodeId> inputs;
                for (std::size_t i = 0; i < expression.elements.size(); ++i)
                    inputs.push_back(lower(expression.elements[i], state, signature->second.parameters[i]));
                for (std::size_t i = 0; i < inputs.size(); ++i)
                    if (state.function.nodes_[inputs[i]].type != signature->second.parameters[i])
                        throw std::runtime_error("wrong function argument type in value-flow input: " +
                                                 expression.name);
                state.function.direct_calls_.insert(expression.name);
                return append(state,
                              {FlowNode::Kind::call, signature->second.result, expression.name, 0, std::move(inputs)});
            }
            if (expression.kind != syntax::ValueExpr::Kind::match)
                throw std::logic_error("unknown Fine value expression");

            if (expression.elements.empty())
                throw std::runtime_error("match without scrutinee in value-flow input");
            if (expression.elements.front().kind == syntax::ValueExpr::Kind::name) {
                auto proof = state.proof_locals.find(expression.elements.front().name);
                if (proof != state.proof_locals.end())
                    return lower_staged_proof_match(expression, *proof->second, state, expected);
            }
            FlowNodeId scrutinee = lower(expression.elements.front(), state);
            FlowType scrutinee_type = state.function.nodes_[scrutinee].type;
            if (scrutinee_type.kind != syntax::ValueType::Kind::enumeration)
                throw std::runtime_error("value-flow match scrutinee is not an enum");
            FlowNode match;
            match.kind = FlowNode::Kind::match;
            match.inputs.push_back(scrutinee);
            for (std::size_t i = 0; i < expression.match_constructors.size(); ++i) {
                auto constructor = constructors_.find(expression.match_constructors[i]);
                if (constructor == constructors_.end() || constructor->second.enumeration != scrutinee_type.name)
                    throw std::runtime_error("wrong match constructor in value-flow input: " +
                                             expression.match_constructors[i]);
                if (expression.match_binders[i].size() != constructor->second.fields.size())
                    throw std::runtime_error("wrong match binder arity in value-flow input");
                std::vector<std::pair<std::string, std::optional<std::pair<FlowLocalId, FlowType>>>> saved;
                FlowMatchArm arm;
                arm.constructor = expression.match_constructors[i];
                for (std::size_t field_index = 0; field_index < expression.match_binders[i].size(); ++field_index) {
                    std::string const &name = expression.match_binders[i][field_index];
                    auto old = state.locals.find(name);
                    saved.push_back({name, old == state.locals.end() ? std::nullopt : std::optional(old->second)});
                    FlowLocalId local = state.next_local++;
                    state.locals[name] = {local, constructor->second.fields[field_index]};
                    arm.binders.push_back(local);
                    arm.binder_types.push_back(constructor->second.fields[field_index]);
                }
                arm.body = lower(expression.elements[i + 1], state, expected);
                FlowType body_type = state.function.nodes_[arm.body].type;
                if (i == 0)
                    match.type = body_type;
                else if (match.type != body_type)
                    throw std::runtime_error("match arms have different value-flow types");
                for (auto const &[name, old] : saved) {
                    if (old)
                        state.locals[name] = *old;
                    else
                        state.locals.erase(name);
                }
                match.arms.push_back(std::move(arm));
            }
            return append(state, std::move(match));
        }

        bool same_index_syntax(syntax::ValueExpr const &left, syntax::ValueExpr const &right) {
            if (left.kind != right.kind || left.name != right.name || left.integer_text != right.integer_text ||
                left.boolean_value != right.boolean_value || left.elements.size() != right.elements.size())
                return false;
            for (std::size_t i = 0; i < left.elements.size(); ++i)
                if (!same_index_syntax(left.elements[i], right.elements[i]))
                    return false;
            return true;
        }

        bool bind_proof_index(syntax::ValueExpr const &pattern, syntax::ValueExpr const &target,
                              std::set<std::string> const &parameters,
                              std::map<std::string, syntax::ValueExpr const *> &bindings) {
            if (pattern.kind == syntax::ValueExpr::Kind::name && parameters.contains(pattern.name)) {
                auto [found, inserted] = bindings.emplace(pattern.name, &target);
                return inserted || same_index_syntax(*found->second, target);
            }
            if (pattern.kind != target.kind || pattern.name != target.name ||
                pattern.integer_text != target.integer_text || pattern.boolean_value != target.boolean_value ||
                pattern.elements.size() != target.elements.size())
                return false;
            for (std::size_t i = 0; i < pattern.elements.size(); ++i)
                if (!bind_proof_index(pattern.elements[i], target.elements[i], parameters, bindings))
                    return false;
            return true;
        }

        FlowNodeId lower_staged_proof_match(syntax::ValueExpr const &expression, syntax::ProofType const &proof,
                                            FunctionState &state, std::optional<FlowType> expected) {
            if (proof.kind != syntax::ProofType::Kind::inductive)
                throw std::runtime_error("value-flow proof match scrutinee is not indexed evidence");
            if (expression.match_constructors.empty()) {
                if (!expected)
                    throw std::runtime_error("empty proof match has no expected value-flow type");
                return append(state, {FlowNode::Kind::bottom, *expected});
            }
            if (expression.match_constructors.size() != 1)
                throw std::runtime_error("unresolved proof match reached value-flow lowering");
            auto signature = proof_constructors_.find(expression.match_constructors.front());
            if (signature == proof_constructors_.end() || signature->second.family != proof.name)
                throw std::runtime_error("wrong staged proof constructor in value-flow input");
            auto const &constructor = signature->second;
            if (expression.match_binders.front().size() != constructor.parameters.size() + constructor.explicit_proofs)
                throw std::runtime_error("wrong staged proof match binder arity in value-flow input");

            std::set<std::string> parameters;
            for (auto const &parameter : constructor.parameters)
                parameters.insert(parameter.name);
            std::map<std::string, syntax::ValueExpr const *> parameter_bindings;
            for (std::size_t i = 0; i < constructor.result_indices.size(); ++i) {
                auto trial = parameter_bindings;
                if (bind_proof_index(constructor.result_indices[i], proof.arguments.at(i), parameters, trial))
                    parameter_bindings = std::move(trial);
            }

            std::vector<std::pair<std::string, std::optional<FlowNodeId>>> saved;
            for (std::size_t i = 0; i < constructor.parameters.size(); ++i) {
                std::string const &binder = expression.match_binders.front()[i];
                auto old = state.aliases.find(binder);
                saved.push_back({binder, old == state.aliases.end() ? std::nullopt : std::optional(old->second)});
                auto binding = parameter_bindings.find(constructor.parameters[i].name);
                if (binding != parameter_bindings.end())
                    state.aliases[binder] = lower(*binding->second, state);
                else
                    state.aliases.erase(binder);
            }
            FlowNodeId body = lower(expression.elements.at(1), state);
            for (auto const &[name, old] : saved) {
                if (old)
                    state.aliases[name] = *old;
                else
                    state.aliases.erase(name);
            }
            return body;
        }

        std::string node_key(ValueFlowFunction const &function, FlowNodeId id) const {
            FlowNode const &node = function.nodes_[id];
            std::ostringstream key;
            key << static_cast<int>(node.kind) << type_key(node.type) << field(node.payload) << node.local << '[';
            for (FlowNodeId input : node.inputs)
                key << field(node_key(function, input));
            key << "]{";
            for (auto const &arm : node.arms) {
                key << field(arm.constructor) << '[';
                for (FlowLocalId binder : arm.binders)
                    key << binder << ',';
                key << "](";
                for (auto const &type : arm.binder_types)
                    key << field(type_key(type));
                key << ')' << field(node_key(function, arm.body));
            }
            return key.str() + '}';
        }

        ValueFlowFunction lower_function(syntax::FunctionDecl const &declaration) {
            FunctionState state;
            state.function.name_ = declaration.name;
            state.function.result_type_ = flow_type(declaration.result_type);
            for (auto const &parameter : declaration.parameters) {
                FlowType type = flow_type(parameter.type);
                FlowLocalId local = state.next_local++;
                if (!state.locals.emplace(parameter.name, std::pair{local, type}).second)
                    throw std::runtime_error("duplicate value-flow parameter: " + parameter.name);
                state.function.parameters_.push_back(std::move(type));
            }
            for (auto const &coeffect : declaration.coeffects)
                state.proof_locals.emplace(coeffect.name, &coeffect.type);
            state.function.root_ = lower(declaration.body, state, state.function.result_type_);
            if (state.function.nodes_[state.function.root_].type != state.function.result_type_)
                throw std::runtime_error("value-flow function result type mismatch: " + declaration.name);
            std::ostringstream key;
            key << "fine-value-flow-v1" << field(declaration.name) << '[';
            for (auto const &parameter : state.function.parameters_)
                key << field(type_key(parameter));
            key << ']' << type_key(state.function.result_type_)
                << field(node_key(state.function, state.function.root_));
            state.function.semantic_key_ = key.str();
            return std::move(state.function);
        }

        void build_sccs(ValueFlowProgram &program) {
            std::map<std::string, int> index;
            std::map<std::string, int> low;
            std::set<std::string> on_stack;
            std::vector<std::string> stack;
            int next = 0;
            std::function<void(std::string const &)> visit = [&](std::string const &name) {
                index[name] = low[name] = next++;
                stack.push_back(name);
                on_stack.insert(name);
                for (auto const &callee : program.functions_.at(name).direct_calls()) {
                    if (!program.functions_.contains(callee))
                        continue;
                    if (!index.contains(callee)) {
                        visit(callee);
                        low[name] = std::min(low[name], low[callee]);
                    }
                    else if (on_stack.contains(callee))
                        low[name] = std::min(low[name], index[callee]);
                }
                if (low[name] != index[name])
                    return;
                CallScc component;
                while (true) {
                    std::string member = stack.back();
                    stack.pop_back();
                    on_stack.erase(member);
                    component.functions.push_back(member);
                    if (member == name)
                        break;
                }
                std::sort(component.functions.begin(), component.functions.end());
                program.sccs_.push_back(std::move(component));
            };
            for (auto const &[name, _] : program.functions_)
                if (!index.contains(name))
                    visit(name);
            for (std::size_t i = 0; i < program.sccs_.size(); ++i)
                for (auto const &name : program.sccs_[i].functions)
                    program.function_sccs_[name] = i;
            for (std::size_t i = 0; i < program.sccs_.size(); ++i) {
                std::set<std::size_t> dependencies;
                std::ostringstream key;
                key << "fine-call-scc-v1";
                for (auto const &name : program.sccs_[i].functions) {
                    key << field(program.functions_.at(name).semantic_key());
                    for (auto const &callee : program.functions_.at(name).direct_calls()) {
                        std::size_t dependency = program.function_sccs_.at(callee);
                        if (dependency != i)
                            dependencies.insert(dependency);
                    }
                }
                program.sccs_[i].dependencies.assign(dependencies.begin(), dependencies.end());
                program.sccs_[i].semantic_key = key.str();
            }
        }
    };

    ValueFlowProgram build_value_flow(syntax::Document const &document) {
        return ValueFlowBuilder(document).build();
    }

}  // namespace fine::stage
