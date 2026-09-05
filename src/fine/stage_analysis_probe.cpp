#include "stage_analysis_probe.h"

#include "parser.h"
#include "value_flow.h"

#include <algorithm>
#include <map>
#include <ostream>
#include <string_view>

namespace fine::stage {

    namespace {
        std::string bits(std::vector<bool> const &values) {
            std::string result;
            result.reserve(values.size());
            for (bool value : values)
                result.push_back(value ? '1' : '0');
            return result;
        }
    }  // namespace

    int run_stage_analysis_probe(std::ostream &output) {
        auto parse = [](std::string_view source) { return build_value_flow(syntax::parse(source)); };
        ValueFlowProgram first = parse(R"fine(
enum Flag { off, on }
function leaf(value: Bool) -> Bool { value }
function caller(value: Bool) -> Bool { leaf(value) }
function unrelated() -> Bool { true }
)fine");
        StageAnalysisCache cache;
        StageAnalysisResult cold = cache.analyze(first);
        StageAnalysisResult warm = cache.analyze(first);
        auto count = [](StageAnalysisResult const &result, bool hit) {
            return std::count_if(result.cache_events.begin(), result.cache_events.end(),
                                 [&](StageCacheEvent const &event) { return event.hit == hit; });
        };
        output << "cold-misses: " << count(cold, false) << '\n';
        output << "warm-hits: " << count(warm, true) << '\n';

        ValueFlowProgram changed = parse(R"fine(
enum Flag { off, on }
function leaf(value: Bool) -> Bool { true }
function caller(value: Bool) -> Bool { leaf(value) }
function unrelated() -> Bool { true }
)fine");
        StageAnalysisResult invalidated = cache.analyze(changed);
        std::map<std::string, bool> hit_by_function;
        for (auto const &event : invalidated.cache_events)
            for (auto const &name : event.functions)
                hit_by_function[name] = event.hit;
        output << "changed-leaf-miss: " << (!hit_by_function.at("leaf") ? "true" : "false") << '\n';
        output << "reverse-caller-miss: " << (!hit_by_function.at("caller") ? "true" : "false") << '\n';
        output << "unrelated-hit: " << (hit_by_function.at("unrelated") ? "true" : "false") << '\n';

        ValueFlowProgram renamed = parse(R"fine(
enum Flag { off, on }
// Local spelling and trivia do not enter the resolved flow key.
function leaf(other: Bool) -> Bool { true }
function caller(argument: Bool) -> Bool { leaf(argument) }
function unrelated() -> Bool { true }
)fine");
        StageAnalysisResult stable = cache.analyze(renamed);
        output << "local-renaming-hits: " << count(stable, true) << '\n';

        ValueFlowProgram same_summary = parse(R"fine(
enum Flag { off, on }
function leaf(value: Bool) -> Bool { false }
function caller(value: Bool) -> Bool { leaf(value) }
function unrelated() -> Bool { true }
)fine");
        StageAnalysisResult stopped = cache.analyze(same_summary);
        hit_by_function.clear();
        for (auto const &event : stopped.cache_events)
            for (auto const &name : event.functions)
                hit_by_function[name] = event.hit;
        output << "same-summary-leaf-miss: " << (!hit_by_function.at("leaf") ? "true" : "false") << '\n';
        output << "same-summary-caller-hit: " << (hit_by_function.at("caller") ? "true" : "false") << '\n';

        ValueFlowProgram mutual = parse(R"fine(
enum Flag { off, on }
function left(flag: Flag, value: Bool) -> Bool {
  match flag { off => value, on => right(off, value), }
}
function right(flag: Flag, value: Bool) -> Bool { left(flag, value) }
)fine");
        StageAnalysisResult mutual_result = cache.analyze(mutual);
        std::size_t left_scc = mutual.function_sccs().at("left");
        output << "mutual-scc-size: " << mutual.sccs()[left_scc].functions.size() << '\n';
        output << "mutual-left-dependencies: " << bits(mutual_result.functions.at("left").result_parameters) << '\n';
        output << "mutual-right-dependencies: " << bits(mutual_result.functions.at("right").result_parameters) << '\n';
        return 0;
    }

}  // namespace fine::stage
