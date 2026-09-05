#include "value_definition_plan.h"

#include "elaboration_internal.h"

#include <map>
#include <queue>
#include <set>
#include <stdexcept>

namespace fine::elaboration {

    namespace {

        void collect_value_calls(syntax::ValueExpr const &expression, std::map<std::string, std::size_t> const &names,
                                 std::set<std::size_t> &calls) {
            if (expression.kind == syntax::ValueExpr::Kind::call) {
                auto function = names.find(expression.name);
                if (function != names.end())
                    calls.insert(function->second);
            }
            for (auto const &element : expression.elements)
                collect_value_calls(element, names, calls);
        }

        void collect_value_calls(syntax::ProofType const &type, std::map<std::string, std::size_t> const &names,
                                 std::set<std::size_t> &calls) {
            if (type.kind == syntax::ProofType::Kind::identity) {
                collect_value_calls(type.left, names, calls);
                collect_value_calls(type.right, names, calls);
                return;
            }
            for (auto const &argument : type.arguments)
                collect_value_calls(argument, names, calls);
        }

    }  // namespace

    std::vector<ValueDefinitionGroup> value_definition_groups(std::vector<syntax::FunctionDecl> const &functions) {
        std::map<std::string, std::size_t> names;
        for (std::size_t i = 0; i < functions.size(); ++i)
            names.emplace(functions[i].name, i);

        std::vector<std::set<std::size_t>> body_dependencies(functions.size());
        std::vector<std::set<std::size_t>> coeffect_dependencies(functions.size());
        std::vector<std::set<std::size_t>> preparation_dependencies(functions.size());
        for (std::size_t caller = 0; caller < functions.size(); ++caller) {
            collect_value_calls(functions[caller].body, names, body_dependencies[caller]);
            preparation_dependencies[caller] = body_dependencies[caller];
            for (auto const &coeffect : functions[caller].coeffects) {
                collect_value_calls(coeffect.type, names, coeffect_dependencies[caller]);
                preparation_dependencies[caller].insert(coeffect_dependencies[caller].begin(),
                                                        coeffect_dependencies[caller].end());
            }
        }

        // Tarjan gives exact strongly connected body-call components.  The
        // later stable Kahn pass orders those components callee before caller.
        std::vector<int> index(functions.size(), -1);
        std::vector<int> low(functions.size(), -1);
        std::vector<std::size_t> stack;
        std::vector<bool> on_stack(functions.size(), false);
        std::vector<std::vector<std::size_t>> components;
        int next_index = 0;
        auto visit = [&](auto &&self, std::size_t function) -> void {
            index[function] = low[function] = next_index++;
            stack.push_back(function);
            on_stack[function] = true;
            for (std::size_t dependency : body_dependencies[function]) {
                if (index[dependency] == -1) {
                    self(self, dependency);
                    low[function] = std::min(low[function], low[dependency]);
                }
                else if (on_stack[dependency])
                    low[function] = std::min(low[function], index[dependency]);
            }
            if (low[function] != index[function])
                return;
            std::vector<std::size_t> component;
            while (true) {
                std::size_t member = stack.back();
                stack.pop_back();
                on_stack[member] = false;
                component.push_back(member);
                if (member == function)
                    break;
            }
            std::sort(component.begin(), component.end());
            components.push_back(std::move(component));
        };
        for (std::size_t i = 0; i < functions.size(); ++i)
            if (index[i] == -1)
                visit(visit, i);

        std::vector<std::size_t> component_of(functions.size());
        for (std::size_t component = 0; component < components.size(); ++component)
            for (std::size_t member : components[component])
                component_of[member] = component;
        for (std::size_t caller = 0; caller < functions.size(); ++caller)
            for (std::size_t dependency : coeffect_dependencies[caller])
                if (component_of[caller] == component_of[dependency])
                    reject(functions[caller].span,
                           "recursive value-function group cannot use one of its own definitions in a coeffect index");
        std::vector<std::set<std::size_t>> dependents(components.size());
        std::vector<std::size_t> dependency_count(components.size(), 0);
        for (std::size_t caller = 0; caller < functions.size(); ++caller)
            for (std::size_t dependency : preparation_dependencies[caller]) {
                std::size_t caller_component = component_of[caller];
                std::size_t dependency_component = component_of[dependency];
                if (caller_component != dependency_component &&
                    dependents[dependency_component].insert(caller_component).second)
                    ++dependency_count[caller_component];
            }

        using Ready = std::pair<std::size_t, std::size_t>;  // first source member, component
        std::priority_queue<Ready, std::vector<Ready>, std::greater<>> ready;
        for (std::size_t component = 0; component < components.size(); ++component)
            if (dependency_count[component] == 0)
                ready.emplace(components[component].front(), component);
        std::vector<ValueDefinitionGroup> result;
        result.reserve(components.size());
        while (!ready.empty()) {
            std::size_t dependency = ready.top().second;
            ready.pop();
            ValueDefinitionGroup group;
            for (std::size_t member : components[dependency])
                group.push_back(&functions[member]);
            result.push_back(std::move(group));
            for (std::size_t caller : dependents[dependency]) {
                if (--dependency_count[caller] == 0)
                    ready.emplace(components[caller].front(), caller);
            }
        }
        if (result.size() != components.size()) {
            for (std::size_t component = 0; component < components.size(); ++component)
                if (dependency_count[component] != 0)
                    reject(functions[components[component].front()].span,
                           "cyclic value-function coeffect dependency cannot be prepared before definitions");
            throw std::logic_error("component dependency ordering failed without a cycle");
        }
        return result;
    }

}  // namespace fine::elaboration
