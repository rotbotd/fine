#include "value_flow.h"

#include <functional>
#include <sstream>
#include <stdexcept>

namespace fine::stage {

    namespace {
        std::string field(std::string_view value) {
            return std::to_string(value.size()) + ':' + std::string(value);
        }

        std::vector<bool> unite(std::vector<bool> left, std::vector<bool> const &right) {
            if (left.size() != right.size())
                throw std::logic_error("stage dependency vectors have different arities");
            for (std::size_t i = 0; i < left.size(); ++i)
                left[i] = left[i] || right[i];
            return left;
        }

        std::string bits(std::vector<bool> const &values) {
            std::string result;
            result.reserve(values.size());
            for (bool value : values)
                result.push_back(value ? '1' : '0');
            return result;
        }
    }  // namespace

    using Dependencies = std::vector<bool>;
    using LocalDependencies = std::map<FlowLocalId, Dependencies>;

    Dependencies evaluate_node(ValueFlowProgram const &program, ValueFlowFunction const &function, FlowNodeId id,
                               LocalDependencies const &locals,
                               std::map<std::string, StageFunctionSummary> const &summaries) {
        FlowNode const &node = function.nodes()[id];
        std::size_t arity = function.parameters().size();
        if (node.kind == FlowNode::Kind::local)
            return locals.at(node.local);
        if (node.kind == FlowNode::Kind::integer || node.kind == FlowNode::Kind::boolean)
            return Dependencies(arity, false);
        if (node.kind == FlowNode::Kind::match) {
            Dependencies scrutinee = evaluate_node(program, function, node.inputs.front(), locals, summaries);
            Dependencies result = scrutinee;
            for (auto const &arm : node.arms) {
                LocalDependencies arm_locals = locals;
                for (FlowLocalId binder : arm.binders)
                    arm_locals[binder] = scrutinee;
                result = unite(std::move(result), evaluate_node(program, function, arm.body, arm_locals, summaries));
            }
            return result;
        }
        std::vector<Dependencies> inputs;
        for (FlowNodeId input : node.inputs)
            inputs.push_back(evaluate_node(program, function, input, locals, summaries));
        if (node.kind == FlowNode::Kind::call) {
            auto summary = summaries.find(node.payload);
            if (summary == summaries.end())
                throw std::logic_error("callee summary is unavailable: " + node.payload);
            Dependencies result(arity, false);
            for (std::size_t i = 0; i < summary->second.result_parameters.size(); ++i)
                if (summary->second.result_parameters[i])
                    result = unite(std::move(result), inputs[i]);
            return result;
        }
        Dependencies result(arity, false);
        for (auto const &input : inputs)
            result = unite(std::move(result), input);
        return result;
    }

    std::map<std::string, StageFunctionSummary>
    solve_scc(ValueFlowProgram const &program, CallScc const &scc,
              std::map<std::string, StageFunctionSummary> const &available) {
        std::map<std::string, StageFunctionSummary> summaries = available;
        for (auto const &name : scc.functions) {
            std::size_t arity = program.functions().at(name).parameters().size();
            summaries[name] = {Dependencies(arity, false), {}};
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto const &name : scc.functions) {
                ValueFlowFunction const &function = program.functions().at(name);
                LocalDependencies locals;
                for (std::size_t i = 0; i < function.parameters().size(); ++i) {
                    Dependencies dependency(function.parameters().size(), false);
                    dependency[i] = true;
                    locals[i] = std::move(dependency);
                }
                Dependencies next = evaluate_node(program, function, function.root(), locals, summaries);
                if (next != summaries[name].result_parameters) {
                    summaries[name].result_parameters = std::move(next);
                    changed = true;
                }
            }
        }
        std::map<std::string, StageFunctionSummary> result;
        for (auto const &name : scc.functions) {
            StageFunctionSummary summary = summaries.at(name);
            ValueFlowFunction const &function = program.functions().at(name);
            std::vector<StageAbstractValue> runtime_arguments;
            for (auto const &type : function.parameters())
                runtime_arguments.push_back(stage_runtime(type));
            StageEvaluation evaluation = infer_stage(program, name, runtime_arguments);
            summary.runtime_input_result = evaluation.result;
            summary.recursive_call_blocked = evaluation.recursive_call_blocked;
            std::ostringstream fingerprint;
            fingerprint << "fine-stage-summary-v3:" << bits(summary.result_parameters) << ':'
                        << stage_value_key(summary.runtime_input_result) << ':'
                        << (summary.recursive_call_blocked ? '1' : '0');
            if (summary.runtime_input_result.kind != StageAbstractValue::Kind::comptime ||
                summary.recursive_call_blocked) {
                // A flat runtime result does not describe how exact arguments
                // transform. Until that full relational transformer is cached,
                // retain the SCC graph and imported fingerprints so a changed
                // nonconstant callee cannot leave a constant-argument caller
                // stale. A constant result at top is already an exact transfer.
                fingerprint << ':' << field(scc.semantic_key);
                for (std::size_t dependency : scc.dependencies)
                    for (auto const &dependency_name : program.sccs()[dependency].functions)
                        fingerprint << field(dependency_name) << field(available.at(dependency_name).fingerprint);
            }
            summary.fingerprint = fingerprint.str();
            result.emplace(name, std::move(summary));
        }
        return result;
    }

    StageAnalysisResult StageAnalysisCache::analyze(ValueFlowProgram const &program) {
        StageAnalysisResult result;
        std::set<std::size_t> done;
        std::function<void(std::size_t)> analyze_scc = [&](std::size_t index) {
            if (done.contains(index))
                return;
            CallScc const &scc = program.sccs()[index];
            for (std::size_t dependency : scc.dependencies)
                analyze_scc(dependency);
            std::ostringstream key;
            key << scc.semantic_key;
            for (std::size_t dependency : scc.dependencies)
                for (auto const &name : program.sccs()[dependency].functions)
                    key << field(name) << field(result.functions.at(name).fingerprint);
            std::string cache_slot;
            for (auto const &name : scc.functions)
                cache_slot += field(name);
            auto found = entries_.find(cache_slot);
            bool hit = found != entries_.end() && found->second.key == key.str();
            std::map<std::string, StageFunctionSummary> summaries;
            if (hit)
                summaries = found->second.summaries;
            else {
                summaries = solve_scc(program, scc, result.functions);
                entries_[cache_slot] = {key.str(), summaries};
            }
            result.functions.insert(summaries.begin(), summaries.end());
            result.cache_events.push_back({index, hit, scc.functions});
            done.insert(index);
        };
        for (std::size_t i = 0; i < program.sccs().size(); ++i)
            analyze_scc(i);
        return result;
    }

}  // namespace fine::stage
