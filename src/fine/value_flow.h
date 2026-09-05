#pragma once

#include "parser.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fine::stage {

    struct FlowType {
        syntax::ValueType::Kind kind = syntax::ValueType::Kind::integer;
        std::string name;

        friend bool operator==(FlowType const &, FlowType const &) = default;
    };

    using FlowNodeId = std::size_t;
    using FlowLocalId = std::size_t;

    struct FlowMatchArm {
        std::string constructor;
        std::vector<FlowLocalId> binders;
        std::vector<FlowType> binder_types;
        FlowNodeId body = 0;
    };

    struct FlowNode {
        enum class Kind { local, integer, boolean, constructor, equal, call, match };

        Kind kind = Kind::local;
        FlowType type;
        std::string payload;
        FlowLocalId local = 0;
        std::vector<FlowNodeId> inputs;
        std::vector<FlowMatchArm> arms;
    };

    class ValueFlowFunction {
    public:
        std::string const &name() const {
            return name_;
        }
        std::vector<FlowType> const &parameters() const {
            return parameters_;
        }
        FlowType const &result_type() const {
            return result_type_;
        }
        std::vector<FlowNode> const &nodes() const {
            return nodes_;
        }
        FlowNodeId root() const {
            return root_;
        }
        std::set<std::string> const &direct_calls() const {
            return direct_calls_;
        }
        std::string const &semantic_key() const {
            return semantic_key_;
        }

    private:
        friend class ValueFlowBuilder;
        std::string name_;
        std::vector<FlowType> parameters_;
        FlowType result_type_;
        std::vector<FlowNode> nodes_;
        FlowNodeId root_ = 0;
        std::set<std::string> direct_calls_;
        std::string semantic_key_;
    };

    struct CallScc {
        std::vector<std::string> functions;
        std::vector<std::size_t> dependencies;
        std::string semantic_key;
    };

    class ValueFlowProgram {
    public:
        std::map<std::string, ValueFlowFunction> const &functions() const {
            return functions_;
        }
        std::vector<CallScc> const &sccs() const {
            return sccs_;
        }
        std::map<std::string, std::size_t> const &function_sccs() const {
            return function_sccs_;
        }

    private:
        friend class ValueFlowBuilder;
        std::map<std::string, ValueFlowFunction> functions_;
        std::vector<CallScc> sccs_;
        std::map<std::string, std::size_t> function_sccs_;
    };

    ValueFlowProgram build_value_flow(syntax::Document const &document);

    struct StageExactValue {
        enum class Kind { integer, boolean, constructor };

        Kind kind = Kind::integer;
        FlowType type;
        std::string payload;
        std::vector<StageExactValue> fields;

        friend bool operator==(StageExactValue const &, StageExactValue const &) = default;
    };

    struct StageAbstractValue {
        enum class Kind { bottom, comptime, runtime };

        Kind kind = Kind::bottom;
        FlowType type;
        std::optional<StageExactValue> exact;
        // This is an auxiliary executable-edge fact, not a fourth stage: a
        // runtime aggregate may still have a statically known constructor.
        std::string known_constructor;
        std::vector<StageAbstractValue> fields;

        friend bool operator==(StageAbstractValue const &, StageAbstractValue const &) = default;
    };

    struct StageTransferTerm;
    using StageTransferTermPtr = std::shared_ptr<StageTransferTerm const>;

    struct StageTransferArm {
        std::string constructor;
        std::size_t source_arm = 0;
        std::vector<FlowLocalId> binders;
        std::vector<FlowType> binder_types;
        StageTransferTermPtr body;
    };

    struct StageTransferTerm {
        enum class Kind { parameter, bound, exact, constructor, equal, match, call, recursive_call };

        Kind kind = Kind::parameter;
        FlowType type;
        std::string payload;
        FlowLocalId local = 0;
        std::optional<StageExactValue> exact;
        std::vector<StageTransferTermPtr> inputs;
        std::vector<StageTransferArm> arms;
        std::string origin_function;
        FlowNodeId origin_match = 0;
        std::vector<FlowType> callee_parameters;
        StageTransferTermPtr callee_root;
        std::string callee_key;
    };

    struct StageTransfer {
        std::vector<FlowType> parameters;
        FlowType result_type;
        StageTransferTermPtr root;
        std::string key;
    };

    struct StageFunctionSummary {
        std::vector<bool> result_parameters;
        // Result inferred when every formal parameter is runtime-unknown. This
        // is part of the exported cache fingerprint: two constant functions
        // returning different values are not interchangeable even though both
        // have the same empty parameter-dependency relation.
        StageAbstractValue runtime_input_result;
        bool recursive_call_blocked = false;
        StageTransfer transfer;
        std::string fingerprint;
    };

    struct StageMatchEdge {
        std::string function;
        FlowNodeId match = 0;
        std::size_t arm = 0;
        std::string constructor;

        friend bool operator==(StageMatchEdge const &, StageMatchEdge const &) = default;

        friend bool operator<(StageMatchEdge const &left, StageMatchEdge const &right) {
            return std::tie(left.function, left.match, left.arm, left.constructor) <
                   std::tie(right.function, right.match, right.arm, right.constructor);
        }
    };

    struct StageEvaluation {
        StageAbstractValue result;
        std::set<StageMatchEdge> executable_edges;
        // A recursive call is not executed merely because its values happen to
        // look static. Termination permission is a separate analysis.
        bool recursive_call_blocked = false;
    };

    StageAbstractValue stage_bottom(FlowType type);
    StageAbstractValue stage_runtime(FlowType type);
    StageAbstractValue stage_boolean(bool value);
    StageAbstractValue stage_integer(std::string_view value);
    StageAbstractValue stage_constructor(FlowType type, std::string constructor,
                                         std::vector<StageAbstractValue> fields);
    StageAbstractValue join_stage_values(StageAbstractValue const &left, StageAbstractValue const &right);
    StageEvaluation infer_stage(ValueFlowProgram const &program, std::string const &function,
                                std::vector<StageAbstractValue> const &arguments);
    StageTransfer build_stage_transfer(ValueFlowProgram const &program, std::string const &function,
                                       std::map<std::string, StageFunctionSummary> const &available,
                                       std::set<std::string> const &recursive_functions);
    StageEvaluation evaluate_stage_transfer(StageTransfer const &transfer,
                                            std::vector<StageAbstractValue> const &arguments);
    std::string render_stage_value(StageAbstractValue const &value);
    std::string stage_value_key(StageAbstractValue const &value);

    struct StageCacheEvent {
        std::size_t scc = 0;
        bool hit = false;
        std::vector<std::string> functions;
    };

    struct StageAnalysisResult {
        std::map<std::string, StageFunctionSummary> functions;
        std::vector<StageCacheEvent> cache_events;
    };

    // This cache contains Fine-owned summaries only. No source pointers or
    // manager-local Z3 handles cross the boundary.
    class StageAnalysisCache {
    public:
        StageAnalysisResult analyze(ValueFlowProgram const &program);

    private:
        struct CachedScc {
            std::string key;
            std::map<std::string, StageFunctionSummary> summaries;
        };
        std::map<std::string, CachedScc> entries_;
    };

}  // namespace fine::stage
