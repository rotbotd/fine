#include "value_flow.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace fine::stage {

    namespace {
        using Term = StageTransferTerm;
        using TermPtr = StageTransferTermPtr;

        std::string field(std::string_view value) {
            return std::to_string(value.size()) + ':' + std::string(value);
        }

        std::string type_key(FlowType const &type) {
            return std::to_string(static_cast<int>(type.kind)) + field(type.name);
        }

        TermPtr make_term(Term term) {
            return std::make_shared<Term const>(std::move(term));
        }

        TermPtr exact_term(StageExactValue value) {
            Term term;
            term.kind = Term::Kind::exact;
            term.type = value.type;
            term.exact = std::move(value);
            return make_term(std::move(term));
        }

        std::string term_key(TermPtr const &term) {
            std::ostringstream result;
            result << static_cast<int>(term->kind) << field(type_key(term->type)) << field(term->payload) << ':'
                   << term->local << ':' << field(term->origin_function) << ':' << term->origin_match;
            if (term->exact)
                result << field(
                    stage_value_key({StageAbstractValue::Kind::comptime, term->exact->type, *term->exact, {}, {}}));
            result << '[';
            for (auto const &input : term->inputs)
                result << field(term_key(input));
            result << field(term->callee_key);
            result << "]{";
            for (auto const &arm : term->arms) {
                result << field(arm.constructor) << ':' << arm.source_arm << '[';
                for (std::size_t i = 0; i < arm.binders.size(); ++i)
                    result << arm.binders[i] << ':' << field(type_key(arm.binder_types[i]));
                result << ']' << field(term_key(arm.body));
            }
            return result.str() + '}';
        }

        TermPtr make_constructor(FlowType type, std::string name, std::vector<TermPtr> fields) {
            std::vector<StageExactValue> exact_fields;
            for (auto const &field : fields) {
                if (field->kind != Term::Kind::exact)
                    break;
                exact_fields.push_back(*field->exact);
            }
            if (exact_fields.size() == fields.size())
                return exact_term(
                    {StageExactValue::Kind::constructor, std::move(type), std::move(name), std::move(exact_fields)});
            Term term;
            term.kind = Term::Kind::constructor;
            term.type = std::move(type);
            term.payload = std::move(name);
            term.inputs = std::move(fields);
            return make_term(std::move(term));
        }

        TermPtr make_equal(FlowType type, TermPtr left, TermPtr right) {
            if (left->kind == Term::Kind::exact && right->kind == Term::Kind::exact)
                return exact_term(*stage_boolean(left->exact == right->exact).exact);
            Term term;
            term.kind = Term::Kind::equal;
            term.type = std::move(type);
            term.inputs = {std::move(left), std::move(right)};
            return make_term(std::move(term));
        }

        TermPtr reduce_match(Term match) {
            TermPtr const &scrutinee = match.inputs.front();
            std::string constructor;
            if (scrutinee->kind == Term::Kind::exact && scrutinee->exact->kind == StageExactValue::Kind::constructor) {
                constructor = scrutinee->exact->payload;
            }
            else if (scrutinee->kind == Term::Kind::constructor)
                constructor = scrutinee->payload;
            else
                return make_term(std::move(match));

            for (auto const &arm : match.arms) {
                if (arm.constructor != constructor)
                    continue;
                match.arms = {arm};
                return make_term(std::move(match));
            }
            throw std::logic_error("stage transfer constructor has no match arm");
        }

        class TransferBuilder {
        public:
            TransferBuilder(ValueFlowProgram const &program,
                            std::map<std::string, StageFunctionSummary> const &available,
                            std::set<std::string> const &recursive_functions)
                : program_(program), available_(available), recursive_functions_(recursive_functions) {}

            StageTransfer build(std::string const &name) {
                ValueFlowFunction const &function = program_.functions().at(name);
                std::map<FlowLocalId, TermPtr> locals;
                for (std::size_t i = 0; i < function.parameters().size(); ++i) {
                    Term parameter;
                    parameter.kind = Term::Kind::parameter;
                    parameter.type = function.parameters()[i];
                    parameter.local = i;
                    locals.emplace(i, make_term(std::move(parameter)));
                }
                StageTransfer transfer;
                transfer.parameters = function.parameters();
                transfer.result_type = function.result_type();
                transfer.root = lower(function, function.root(), locals);
                transfer.key = "fine-stage-transfer-v1:" + term_key(transfer.root);
                return transfer;
            }

        private:
            ValueFlowProgram const &program_;
            std::map<std::string, StageFunctionSummary> const &available_;
            std::set<std::string> const &recursive_functions_;

            TermPtr lower(ValueFlowFunction const &function, FlowNodeId id,
                          std::map<FlowLocalId, TermPtr> const &locals) {
                FlowNode const &node = function.nodes().at(id);
                if (node.kind == FlowNode::Kind::bottom) {
                    Term bottom;
                    bottom.kind = Term::Kind::bottom;
                    bottom.type = node.type;
                    return make_term(std::move(bottom));
                }
                if (node.kind == FlowNode::Kind::local)
                    return locals.at(node.local);
                if (node.kind == FlowNode::Kind::integer)
                    return exact_term(*stage_integer(node.payload).exact);
                if (node.kind == FlowNode::Kind::boolean)
                    return exact_term(*stage_boolean(node.payload == "true").exact);

                std::vector<TermPtr> inputs;
                for (FlowNodeId input : node.inputs)
                    inputs.push_back(lower(function, input, locals));
                if (node.kind == FlowNode::Kind::constructor)
                    return make_constructor(node.type, node.payload, std::move(inputs));
                if (node.kind == FlowNode::Kind::equal)
                    return make_equal(node.type, inputs[0], inputs[1]);
                if (node.kind == FlowNode::Kind::call) {
                    if (recursive_functions_.contains(node.payload)) {
                        Term recursive;
                        recursive.kind = Term::Kind::recursive_call;
                        recursive.type = node.type;
                        recursive.payload = node.payload;
                        recursive.inputs = std::move(inputs);
                        return make_term(std::move(recursive));
                    }
                    auto found = available_.find(node.payload);
                    if (found == available_.end() || !found->second.transfer.root)
                        throw std::logic_error("stage transfer for callee is unavailable: " + node.payload);
                    Term call;
                    call.kind = Term::Kind::call;
                    call.type = node.type;
                    call.payload = node.payload;
                    call.inputs = std::move(inputs);
                    call.callee_parameters = found->second.transfer.parameters;
                    call.callee_root = found->second.transfer.root;
                    call.callee_key = found->second.transfer.key;
                    return make_term(std::move(call));
                }
                if (node.kind != FlowNode::Kind::match)
                    throw std::logic_error("unknown stage transfer flow node");

                Term match;
                match.kind = Term::Kind::match;
                match.type = node.type;
                match.inputs = std::move(inputs);
                match.origin_function = function.name();
                match.origin_match = id;

                // If the constructor is already visible, lower only its live
                // arm. A recursive call in a dead arm must not contaminate the
                // transfer with a false termination demand.
                std::string known_constructor;
                if (match.inputs.front()->kind == Term::Kind::exact &&
                    match.inputs.front()->exact->kind == StageExactValue::Kind::constructor) {
                    known_constructor = match.inputs.front()->exact->payload;
                }
                else if (match.inputs.front()->kind == Term::Kind::constructor)
                    known_constructor = match.inputs.front()->payload;

                for (std::size_t source_arm = 0; source_arm < node.arms.size(); ++source_arm) {
                    auto const &arm = node.arms[source_arm];
                    if (!known_constructor.empty() && arm.constructor != known_constructor)
                        continue;
                    std::map<FlowLocalId, TermPtr> arm_locals = locals;
                    StageTransferArm transfer_arm;
                    transfer_arm.constructor = arm.constructor;
                    transfer_arm.source_arm = source_arm;
                    transfer_arm.binders = arm.binders;
                    transfer_arm.binder_types = arm.binder_types;
                    for (std::size_t i = 0; i < arm.binders.size(); ++i) {
                        Term bound;
                        bound.kind = Term::Kind::bound;
                        bound.type = arm.binder_types[i];
                        bound.local = arm.binders[i];
                        arm_locals[arm.binders[i]] = make_term(std::move(bound));
                    }
                    transfer_arm.body = lower(function, arm.body, arm_locals);
                    match.arms.push_back(std::move(transfer_arm));
                }
                return reduce_match(std::move(match));
            }
        };

        StageAbstractValue exact_abstract(StageExactValue value) {
            FlowType type = value.type;
            return {StageAbstractValue::Kind::comptime, std::move(type), std::move(value), {}, {}};
        }

        void require_type(StageAbstractValue const &value, FlowType const &type) {
            if (value.type != type)
                throw std::runtime_error("stage transfer argument has the wrong Fine value type");
        }

        class TransferEvaluator {
        public:
            StageEvaluation evaluate(StageTransfer const &transfer, std::vector<StageAbstractValue> const &arguments) {
                if (arguments.size() != transfer.parameters.size())
                    throw std::runtime_error("wrong stage transfer arity");
                for (std::size_t i = 0; i < arguments.size(); ++i)
                    require_type(arguments[i], transfer.parameters[i]);
                arguments_ = arguments;
                return term(transfer.root, {});
            }

        private:
            std::vector<StageAbstractValue> arguments_;

            StageEvaluation term(TermPtr const &value, std::map<FlowLocalId, StageAbstractValue> const &bound) {
                if (value->kind == Term::Kind::bottom)
                    return {stage_bottom(value->type), {}, false};
                if (value->kind == Term::Kind::parameter)
                    return {arguments_.at(value->local), {}, false};
                if (value->kind == Term::Kind::bound)
                    return {bound.at(value->local), {}, false};
                if (value->kind == Term::Kind::exact)
                    return {exact_abstract(*value->exact), {}, false};

                std::vector<StageEvaluation> inputs;
                for (auto const &input : value->inputs)
                    inputs.push_back(term(input, bound));
                StageEvaluation result{stage_bottom(value->type), {}, false};
                auto absorb = [&](StageEvaluation const &part) {
                    result.executable_edges.insert(part.executable_edges.begin(), part.executable_edges.end());
                    result.recursive_call_blocked = result.recursive_call_blocked || part.recursive_call_blocked;
                };
                for (auto const &input : inputs)
                    absorb(input);

                if (value->kind == Term::Kind::recursive_call) {
                    result.result = stage_runtime(value->type);
                    result.recursive_call_blocked = true;
                    return result;
                }
                if (value->kind == Term::Kind::call) {
                    std::vector<StageAbstractValue> arguments;
                    for (auto const &input : inputs)
                        arguments.push_back(input.result);
                    StageTransfer callee{value->callee_parameters, value->type, value->callee_root, value->callee_key};
                    StageEvaluation called = TransferEvaluator().evaluate(callee, arguments);
                    absorb(called);
                    result.result = called.result;
                    return result;
                }
                if (value->kind == Term::Kind::constructor) {
                    std::vector<StageAbstractValue> fields;
                    for (auto const &input : inputs)
                        fields.push_back(input.result);
                    result.result = stage_constructor(value->type, value->payload, std::move(fields));
                    return result;
                }
                if (value->kind == Term::Kind::equal) {
                    if (inputs[0].result.kind == StageAbstractValue::Kind::bottom ||
                        inputs[1].result.kind == StageAbstractValue::Kind::bottom)
                        result.result = stage_bottom(value->type);
                    else if (inputs[0].result.exact && inputs[1].result.exact)
                        result.result = stage_boolean(inputs[0].result.exact == inputs[1].result.exact);
                    else
                        result.result = stage_runtime(value->type);
                    return result;
                }
                if (value->kind != Term::Kind::match)
                    throw std::logic_error("unknown stage transfer term");

                StageAbstractValue const &scrutinee = inputs.front().result;
                if (scrutinee.kind == StageAbstractValue::Kind::bottom)
                    return result;
                std::vector<std::size_t> live;
                std::string known_constructor;
                if (scrutinee.exact) {
                    if (scrutinee.exact->kind != StageExactValue::Kind::constructor)
                        throw std::logic_error("exact transfer match scrutinee is not a constructor");
                    known_constructor = scrutinee.exact->payload;
                }
                else if (!scrutinee.known_constructor.empty())
                    known_constructor = scrutinee.known_constructor;
                if (!known_constructor.empty()) {
                    for (std::size_t i = 0; i < value->arms.size(); ++i)
                        if (value->arms[i].constructor == known_constructor)
                            live.push_back(i);
                    if (live.size() != 1)
                        throw std::logic_error("known transfer constructor has no unique arm");
                }
                else {
                    for (std::size_t i = 0; i < value->arms.size(); ++i)
                        live.push_back(i);
                }
                for (std::size_t arm_index : live) {
                    StageTransferArm const &arm = value->arms[arm_index];
                    result.executable_edges.insert(
                        {value->origin_function, value->origin_match, arm.source_arm, arm.constructor});
                    auto arm_bound = bound;
                    for (std::size_t i = 0; i < arm.binders.size(); ++i) {
                        if (scrutinee.exact)
                            arm_bound[arm.binders[i]] = exact_abstract(scrutinee.exact->fields.at(i));
                        else if (!scrutinee.known_constructor.empty())
                            arm_bound[arm.binders[i]] = scrutinee.fields.at(i);
                        else
                            arm_bound[arm.binders[i]] = stage_runtime(arm.binder_types[i]);
                    }
                    StageEvaluation body = term(arm.body, arm_bound);
                    absorb(body);
                    result.result = join_stage_values(result.result, body.result);
                }
                return result;
            }
        };
    }  // namespace

    StageTransfer build_stage_transfer(ValueFlowProgram const &program, std::string const &function,
                                       std::map<std::string, StageFunctionSummary> const &available,
                                       std::set<std::string> const &recursive_functions) {
        return TransferBuilder(program, available, recursive_functions).build(function);
    }

    StageEvaluation evaluate_stage_transfer(StageTransfer const &transfer,
                                            std::vector<StageAbstractValue> const &arguments) {
        return TransferEvaluator().evaluate(transfer, arguments);
    }

}  // namespace fine::stage
