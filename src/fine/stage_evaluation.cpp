#include "value_flow.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fine::stage {

    namespace {
        using AbstractValue = StageAbstractValue;
        using ExactValue = StageExactValue;

        FlowType boolean_type() {
            return {syntax::ValueType::Kind::boolean, {}};
        }

        FlowType integer_type() {
            return {syntax::ValueType::Kind::integer, {}};
        }

        void require_type(AbstractValue const &value, FlowType const &expected) {
            if (value.type != expected)
                throw std::runtime_error("stage argument has the wrong Fine value type");
            if ((value.kind == AbstractValue::Kind::comptime) != value.exact.has_value())
                throw std::logic_error("invalid exact stage value");
        }

        std::string normalize_integer(std::string_view source) {
            bool negative = !source.empty() && source.front() == '-';
            std::size_t begin = negative ? 1 : 0;
            while (begin < source.size() && source[begin] == '0')
                ++begin;
            if (begin == source.size())
                return "0";
            return std::string(negative ? "-" : "") + std::string(source.substr(begin));
        }

        AbstractValue exact(ExactValue value) {
            return {AbstractValue::Kind::comptime, value.type, std::move(value), {}, {}};
        }

        class Evaluator {
        public:
            explicit Evaluator(ValueFlowProgram const &program) : program_(program) {}

            StageEvaluation call(std::string const &name, std::vector<AbstractValue> const &arguments) {
                auto found = program_.functions().find(name);
                if (found == program_.functions().end())
                    throw std::runtime_error("unknown function in stage analysis: " + name);
                ValueFlowFunction const &function = found->second;
                if (arguments.size() != function.parameters().size())
                    throw std::runtime_error("wrong function arity in stage analysis: " + name);
                for (std::size_t i = 0; i < arguments.size(); ++i)
                    require_type(arguments[i], function.parameters()[i]);

                if (std::find(active_.begin(), active_.end(), name) != active_.end())
                    return {stage_runtime(function.result_type()), {}, true};

                active_.push_back(name);
                std::map<FlowLocalId, AbstractValue> locals;
                for (std::size_t i = 0; i < arguments.size(); ++i)
                    locals.emplace(i, arguments[i]);
                StageEvaluation result = node(function, function.root(), locals);
                active_.pop_back();
                return result;
            }

        private:
            ValueFlowProgram const &program_;
            std::vector<std::string> active_;

            StageEvaluation node(ValueFlowFunction const &function, FlowNodeId id,
                                 std::map<FlowLocalId, AbstractValue> const &locals) {
                FlowNode const &flow = function.nodes().at(id);
                if (flow.kind == FlowNode::Kind::bottom)
                    return {stage_bottom(flow.type), {}, false};
                if (flow.kind == FlowNode::Kind::local)
                    return {locals.at(flow.local), {}, false};
                if (flow.kind == FlowNode::Kind::integer)
                    return {stage_integer(flow.payload), {}, false};
                if (flow.kind == FlowNode::Kind::boolean)
                    return {stage_boolean(flow.payload == "true"), {}, false};

                std::vector<StageEvaluation> inputs;
                for (FlowNodeId input : flow.inputs)
                    inputs.push_back(node(function, input, locals));

                StageEvaluation result;
                result.result = stage_bottom(flow.type);
                auto absorb = [&](StageEvaluation const &part) {
                    result.executable_edges.insert(part.executable_edges.begin(), part.executable_edges.end());
                    result.recursive_call_blocked = result.recursive_call_blocked || part.recursive_call_blocked;
                };
                for (auto const &input : inputs)
                    absorb(input);

                if (flow.kind == FlowNode::Kind::constructor) {
                    std::vector<AbstractValue> fields;
                    for (auto const &input : inputs)
                        fields.push_back(input.result);
                    result.result = stage_constructor(flow.type, flow.payload, std::move(fields));
                    return result;
                }

                if (flow.kind == FlowNode::Kind::equal) {
                    if (inputs[0].result.kind == AbstractValue::Kind::bottom ||
                        inputs[1].result.kind == AbstractValue::Kind::bottom)
                        result.result = stage_bottom(boolean_type());
                    else if (inputs[0].result.exact && inputs[1].result.exact)
                        result.result = stage_boolean(inputs[0].result.exact == inputs[1].result.exact);
                    else
                        result.result = stage_runtime(boolean_type());
                    return result;
                }

                if (flow.kind == FlowNode::Kind::call) {
                    std::vector<AbstractValue> arguments;
                    for (auto const &input : inputs)
                        arguments.push_back(input.result);
                    StageEvaluation called = call(flow.payload, arguments);
                    absorb(called);
                    result.result = called.result;
                    return result;
                }

                if (flow.kind != FlowNode::Kind::match)
                    throw std::logic_error("unknown stage flow node");

                AbstractValue const &scrutinee = inputs.front().result;
                if (scrutinee.kind == AbstractValue::Kind::bottom)
                    return result;

                std::vector<std::size_t> executable;
                std::string known_constructor;
                if (scrutinee.exact) {
                    if (scrutinee.exact->kind != ExactValue::Kind::constructor)
                        throw std::logic_error("compile-time match scrutinee is not a constructor");
                    known_constructor = scrutinee.exact->payload;
                }
                else if (!scrutinee.known_constructor.empty())
                    known_constructor = scrutinee.known_constructor;
                if (!known_constructor.empty()) {
                    for (std::size_t i = 0; i < flow.arms.size(); ++i)
                        if (flow.arms[i].constructor == known_constructor)
                            executable.push_back(i);
                    if (executable.size() != 1)
                        throw std::logic_error("compile-time constructor has no unique match arm");
                }
                else {
                    for (std::size_t i = 0; i < flow.arms.size(); ++i)
                        executable.push_back(i);
                }

                for (std::size_t arm_index : executable) {
                    FlowMatchArm const &arm = flow.arms[arm_index];
                    result.executable_edges.insert({function.name(), id, arm_index, arm.constructor});
                    std::map<FlowLocalId, AbstractValue> arm_locals = locals;
                    for (std::size_t field = 0; field < arm.binders.size(); ++field) {
                        if (scrutinee.exact)
                            arm_locals[arm.binders[field]] = exact(scrutinee.exact->fields.at(field));
                        else if (!scrutinee.known_constructor.empty())
                            arm_locals[arm.binders[field]] = scrutinee.fields.at(field);
                        else
                            arm_locals[arm.binders[field]] = stage_runtime(arm.binder_types.at(field));
                    }
                    StageEvaluation body = node(function, arm.body, arm_locals);
                    absorb(body);
                    result.result = join_stage_values(result.result, body.result);
                }
                return result;
            }
        };

        std::string render_exact(ExactValue const &value) {
            if (value.kind != ExactValue::Kind::constructor)
                return value.payload;
            if (value.fields.empty())
                return value.payload;
            std::string result = value.payload + "(";
            for (std::size_t i = 0; i < value.fields.size(); ++i) {
                if (i)
                    result += ", ";
                result += render_exact(value.fields[i]);
            }
            return result + ")";
        }

        std::string field(std::string_view value) {
            return std::to_string(value.size()) + ':' + std::string(value);
        }

        std::string type_key(FlowType const &type) {
            return std::to_string(static_cast<int>(type.kind)) + field(type.name);
        }

        std::string exact_key(ExactValue const &value) {
            std::string result =
                std::to_string(static_cast<int>(value.kind)) + field(type_key(value.type)) + field(value.payload) + '[';
            for (auto const &child : value.fields)
                result += field(exact_key(child));
            return result + ']';
        }
    }  // namespace

    StageAbstractValue stage_bottom(FlowType type) {
        return {StageAbstractValue::Kind::bottom, std::move(type), std::nullopt, {}, {}};
    }

    StageAbstractValue stage_runtime(FlowType type) {
        return {StageAbstractValue::Kind::runtime, std::move(type), std::nullopt, {}, {}};
    }

    StageAbstractValue stage_boolean(bool value) {
        FlowType type = boolean_type();
        return exact({ExactValue::Kind::boolean, type, value ? "true" : "false", {}});
    }

    StageAbstractValue stage_integer(std::string_view value) {
        FlowType type = integer_type();
        return exact({ExactValue::Kind::integer, type, normalize_integer(value), {}});
    }

    StageAbstractValue stage_constructor(FlowType type, std::string constructor,
                                         std::vector<StageAbstractValue> fields) {
        for (auto const &field : fields)
            if (field.kind == StageAbstractValue::Kind::bottom)
                return stage_bottom(std::move(type));
        std::vector<StageExactValue> exact_fields;
        for (auto const &field : fields) {
            if (!field.exact)
                break;
            exact_fields.push_back(*field.exact);
        }
        if (exact_fields.size() == fields.size())
            return exact(
                {StageExactValue::Kind::constructor, std::move(type), std::move(constructor), std::move(exact_fields)});
        return {StageAbstractValue::Kind::runtime, std::move(type), std::nullopt, std::move(constructor),
                std::move(fields)};
    }

    StageAbstractValue join_stage_values(StageAbstractValue const &left, StageAbstractValue const &right) {
        require_type(left, right.type);
        require_type(right, left.type);
        if (left.kind == StageAbstractValue::Kind::bottom)
            return right;
        if (right.kind == StageAbstractValue::Kind::bottom)
            return left;
        if (left.exact && right.exact && left.exact == right.exact)
            return left;

        auto constructor = [](StageAbstractValue const &value) -> std::string {
            if (value.exact && value.exact->kind == StageExactValue::Kind::constructor)
                return value.exact->payload;
            return value.known_constructor;
        };
        std::string left_constructor = constructor(left);
        std::string right_constructor = constructor(right);
        if (!left_constructor.empty() && left_constructor == right_constructor) {
            std::vector<StageAbstractValue> left_fields = left.fields;
            std::vector<StageAbstractValue> right_fields = right.fields;
            if (left.exact) {
                left_fields.clear();
                for (auto const &field : left.exact->fields)
                    left_fields.push_back(exact(field));
            }
            if (right.exact) {
                right_fields.clear();
                for (auto const &field : right.exact->fields)
                    right_fields.push_back(exact(field));
            }
            if (left_fields.size() != right_fields.size())
                throw std::logic_error("same stage constructor has different arity");
            std::vector<StageAbstractValue> fields;
            for (std::size_t i = 0; i < left_fields.size(); ++i)
                fields.push_back(join_stage_values(left_fields[i], right_fields[i]));
            return stage_constructor(left.type, left_constructor, std::move(fields));
        }
        return stage_runtime(left.type);
    }

    StageEvaluation infer_stage(ValueFlowProgram const &program, std::string const &function,
                                std::vector<StageAbstractValue> const &arguments) {
        return Evaluator(program).call(function, arguments);
    }

    std::string render_stage_value(StageAbstractValue const &value) {
        if (value.kind == StageAbstractValue::Kind::bottom)
            return "bottom";
        if (value.kind == StageAbstractValue::Kind::runtime)
            return "runtime";
        return "comptime(" + render_exact(*value.exact) + ")";
    }

    std::string stage_value_key(StageAbstractValue const &value) {
        std::string result = std::to_string(static_cast<int>(value.kind)) + field(type_key(value.type));
        if (value.exact)
            result += field(exact_key(*value.exact));
        result += field(value.known_constructor);
        for (auto const &child : value.fields)
            result += field(stage_value_key(child));
        return result;
    }

}  // namespace fine::stage
