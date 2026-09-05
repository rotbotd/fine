#include "value_definition_plan.h"

#include "elaboration_internal.h"

#include <functional>
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

    std::vector<syntax::FunctionDecl const *>
    value_definition_order(std::vector<syntax::FunctionDecl> const &functions) {
        std::map<std::string, std::size_t> names;
        for (std::size_t i = 0; i < functions.size(); ++i)
            names.emplace(functions[i].name, i);

        std::vector<std::set<std::size_t>> dependents(functions.size());
        std::vector<std::set<std::size_t>> dependencies_by_caller(functions.size());
        std::vector<std::size_t> dependency_count(functions.size(), 0);
        for (std::size_t caller = 0; caller < functions.size(); ++caller) {
            std::set<std::size_t> dependencies;
            collect_value_calls(functions[caller].body, names, dependencies);
            for (auto const &clause : functions[caller].ensures)
                collect_value_calls(clause, names, dependencies);
            for (auto const &coeffect : functions[caller].coeffects)
                collect_value_calls(coeffect.type, names, dependencies);
            dependencies.erase(caller);
            dependencies_by_caller[caller] = dependencies;
            dependency_count[caller] = dependencies.size();
            for (std::size_t dependency : dependencies)
                dependents[dependency].insert(caller);
        }

        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t i = 0; i < dependency_count.size(); ++i)
            if (dependency_count[i] == 0)
                ready.push(i);
        std::vector<syntax::FunctionDecl const *> result;
        result.reserve(functions.size());
        while (!ready.empty()) {
            std::size_t dependency = ready.top();
            ready.pop();
            result.push_back(&functions[dependency]);
            for (std::size_t caller : dependents[dependency])
                if (--dependency_count[caller] == 0)
                    ready.push(caller);
        }
        if (result.size() != functions.size()) {
            std::vector<unsigned char> color(functions.size(), 0);
            std::size_t cyclic = functions.size();
            std::function<bool(std::size_t)> visit = [&](std::size_t caller) {
                color[caller] = 1;
                for (std::size_t dependency : dependencies_by_caller[caller]) {
                    if (color[dependency] == 1) {
                        cyclic = dependency;
                        return true;
                    }
                    if (color[dependency] == 0 && visit(dependency))
                        return true;
                }
                color[caller] = 2;
                return false;
            };
            for (std::size_t i = 0; i < functions.size() && cyclic == functions.size(); ++i)
                if (color[i] == 0)
                    visit(i);
            if (cyclic != functions.size())
                reject(functions[cyclic].span,
                       "cyclic value-function dependency requires a checked recursive definition group");
            throw std::logic_error("dependency ordering failed without a cycle");
        }
        return result;
    }

}  // namespace fine::elaboration
