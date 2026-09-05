#pragma once

#include "parser.h"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
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

    struct StageDependencySummary {
        std::vector<bool> result_parameters;
        std::string fingerprint;
    };

    struct StageCacheEvent {
        std::size_t scc = 0;
        bool hit = false;
        std::vector<std::string> functions;
    };

    struct StageAnalysisResult {
        std::map<std::string, StageDependencySummary> functions;
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
            std::map<std::string, StageDependencySummary> summaries;
        };
        std::map<std::string, CachedScc> entries_;
    };

}  // namespace fine::stage
