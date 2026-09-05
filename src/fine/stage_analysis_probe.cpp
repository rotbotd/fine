#include "stage_analysis_probe.h"

#include "parser.h"
#include "runtime.h"
#include "value_flow.h"

#include <algorithm>
#include <map>
#include <ostream>
#include <sstream>
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
        output << "changed-constant-caller-miss: " << (!hit_by_function.at("caller") ? "true" : "false") << '\n';

        ValueFlowProgram same_export = parse(R"fine(
enum Flag { off, on }
function leaf(value: Bool) -> Bool { false == true }
function caller(value: Bool) -> Bool { leaf(value) }
function unrelated() -> Bool { true }
)fine");
        StageAnalysisResult export_stopped = cache.analyze(same_export);
        hit_by_function.clear();
        for (auto const &event : export_stopped.cache_events)
            for (auto const &name : event.functions)
                hit_by_function[name] = event.hit;
        output << "same-export-leaf-miss: " << (!hit_by_function.at("leaf") ? "true" : "false") << '\n';
        output << "same-export-caller-hit: " << (hit_by_function.at("caller") ? "true" : "false") << '\n';

        StageAnalysisCache relational_cache;
        relational_cache.analyze(parse(R"fine(
function leaf(value: Bool) -> Bool { value }
function exact_caller() -> Bool { leaf(true) }
)fine"));
        StageAnalysisResult changed_transform = relational_cache.analyze(parse(R"fine(
function leaf(value: Bool) -> Bool { value == false }
function exact_caller() -> Bool { leaf(true) }
)fine"));
        hit_by_function.clear();
        for (auto const &event : changed_transform.cache_events)
            for (auto const &name : event.functions)
                hit_by_function[name] = event.hit;
        output << "nonconstant-transform-caller-miss: " << (!hit_by_function.at("exact_caller") ? "true" : "false")
               << '\n';
        output << "nonconstant-transform-caller-result: "
               << render_stage_value(changed_transform.functions.at("exact_caller").runtime_input_result) << '\n';

        ValueFlowProgram executable = parse(R"fine(
enum Flag { off, on }
enum Box { box(Bool) }
enum Choice { left(Bool), right(Bool) }
enum Option { none, some(Bool) }
function identity(value: Bool) -> Bool { value }
function through_identity(value: Bool) -> Bool { identity(value) }
function dead_arm(value: Bool) -> Bool {
  match off { off => true, on => value, }
}
function live_join(flag: Flag) -> Bool {
  match flag { off => true, on => false, }
}
function unpack(value: Box) -> Bool {
  match value { box(inner) => inner, }
}
function boxed() -> Bool { unpack(box(true)) }
function known_tag(value: Bool) -> Bool {
  match left(value) { left(inner) => true, right(inner) => false, }
}
function make_left(value: Bool) -> Choice { left(value) }
function inspect_left(value: Bool) -> Bool {
  match make_left(value) { left(inner) => true, right(inner) => false, }
}
function choose(value: Option, fallback: Bool) -> Bool {
  match value { none => fallback, some(item) => fallback, }
}
function preserve(source: Option, choice: Option) -> Bool {
  match source { none => false, some(item) => choose(choice, item), }
}
function capture_avoiding(choice: Option) -> Bool { preserve(some(true), choice) }
function spin(flag: Flag) -> Bool {
  match flag { off => true, on => spin(flag), }
}
function ignores(value: Bool) -> Bool { true }
function strict_argument(flag: Flag) -> Bool { ignores(spin(flag)) }
function normalized_integer() -> Bool { -0 == 0 }
)fine");
        StageAnalysisResult executable_analysis = StageAnalysisCache().analyze(executable);
        auto evaluate = [&](std::string const &name, std::vector<StageAbstractValue> arguments) {
            return evaluate_stage_transfer(executable_analysis.functions.at(name).transfer, arguments);
        };
        StageEvaluation known_identity = evaluate("through_identity", {stage_boolean(true)});
        StageEvaluation runtime_identity =
            evaluate("through_identity", {stage_runtime({syntax::ValueType::Kind::boolean, {}})});
        StageEvaluation dead = evaluate("dead_arm", {stage_runtime({syntax::ValueType::Kind::boolean, {}})});
        StageEvaluation live = evaluate("live_join", {stage_runtime({syntax::ValueType::Kind::enumeration, "Flag"})});
        StageEvaluation boxed = evaluate("boxed", {});
        StageEvaluation known_tag = evaluate("known_tag", {stage_runtime({syntax::ValueType::Kind::boolean, {}})});
        StageEvaluation cross_call_known_tag =
            evaluate("inspect_left", {stage_runtime({syntax::ValueType::Kind::boolean, {}})});
        StageEvaluation capture_avoiding =
            evaluate("capture_avoiding", {stage_runtime({syntax::ValueType::Kind::enumeration, "Option"})});
        StageEvaluation strict_argument =
            evaluate("strict_argument", {stage_runtime({syntax::ValueType::Kind::enumeration, "Flag"})});
        StageEvaluation normalized = evaluate("normalized_integer", {});
        output << "known-identity: " << render_stage_value(known_identity.result) << '\n';
        output << "runtime-identity: " << render_stage_value(runtime_identity.result) << '\n';
        output << "dead-arm-result: " << render_stage_value(dead.result) << '\n';
        output << "dead-arm-edges: " << dead.executable_edges.size() << '\n';
        output << "live-join-result: " << render_stage_value(live.result) << '\n';
        output << "live-join-edges: " << live.executable_edges.size() << '\n';
        output << "constructor-field-result: " << render_stage_value(boxed.result) << '\n';
        output << "known-tag-runtime-payload: " << render_stage_value(known_tag.result) << '\n';
        output << "known-tag-edges: " << known_tag.executable_edges.size() << '\n';
        output << "cross-call-known-tag: " << render_stage_value(cross_call_known_tag.result) << '\n';
        output << "cross-call-known-tag-edges: " << cross_call_known_tag.executable_edges.size() << '\n';
        output << "capture-avoiding-result: " << render_stage_value(capture_avoiding.result) << '\n';
        output << "capture-avoiding-edges: " << capture_avoiding.executable_edges.size() << '\n';
        output << "strict-argument-result: " << render_stage_value(strict_argument.result) << '\n';
        output << "strict-argument-recursion-blocked: " << (strict_argument.recursive_call_blocked ? "true" : "false")
               << '\n';
        output << "normalized-integer-equality: " << render_stage_value(normalized.result) << '\n';

        bool oracle_equal = true;
        auto compare_oracle = [&](std::string const &name, std::vector<StageAbstractValue> const &arguments,
                                  StageEvaluation const &transferred) {
            StageEvaluation direct = infer_stage(executable, name, arguments);
            oracle_equal = oracle_equal && direct.result == transferred.result &&
                           direct.executable_edges == transferred.executable_edges &&
                           direct.recursive_call_blocked == transferred.recursive_call_blocked;
        };
        compare_oracle("through_identity", {stage_boolean(true)}, known_identity);
        compare_oracle("through_identity", {stage_runtime({syntax::ValueType::Kind::boolean, {}})}, runtime_identity);
        compare_oracle("dead_arm", {stage_runtime({syntax::ValueType::Kind::boolean, {}})}, dead);
        compare_oracle("live_join", {stage_runtime({syntax::ValueType::Kind::enumeration, "Flag"})}, live);
        compare_oracle("boxed", {}, boxed);
        compare_oracle("inspect_left", {stage_runtime({syntax::ValueType::Kind::boolean, {}})}, cross_call_known_tag);
        compare_oracle("capture_avoiding", {stage_runtime({syntax::ValueType::Kind::enumeration, "Option"})},
                       capture_avoiding);
        compare_oracle("strict_argument", {stage_runtime({syntax::ValueType::Kind::enumeration, "Flag"})},
                       strict_argument);
        compare_oracle("normalized_integer", {}, normalized);
        output << "transfer-matches-direct-oracle: " << (oracle_equal ? "true" : "false") << '\n';

        ValueFlowProgram staged_proof = parse(R"fine(
enum Flag { off, on }
proof inductive Tagged(value: Flag) {
  tagged(field: Flag) -> Tagged(field);
}
proof inductive Never() {}
function recover(value: Flag) -> Flag
  takes [evidence: Tagged(value)]
{
  match evidence { tagged(field) => field, }
}
function eliminate_never() -> Bool
  takes [impossible: Never()]
{
  match impossible {}
}
function keep_bool(value: Bool) -> Bool { value }
function eliminate_never_as_argument() -> Bool
  takes [impossible: Never()]
{
  keep_bool(match impossible {})
}
)fine");
        StageAnalysisResult staged_proof_analysis = StageAnalysisCache().analyze(staged_proof);
        StageEvaluation staged_proof_result =
            evaluate_stage_transfer(staged_proof_analysis.functions.at("recover").transfer,
                                    {stage_runtime({syntax::ValueType::Kind::enumeration, "Flag"})});
        output << "staged-proof-residual-result: " << render_stage_value(staged_proof_result.result) << '\n';
        output << "staged-proof-residual-dependencies: "
               << bits(staged_proof_analysis.functions.at("recover").result_parameters) << '\n';
        StageEvaluation impossible_result =
            evaluate_stage_transfer(staged_proof_analysis.functions.at("eliminate_never").transfer, {});
        output << "impossible-proof-residual-result: " << render_stage_value(impossible_result.result) << '\n';
        StageEvaluation impossible_argument =
            evaluate_stage_transfer(staged_proof_analysis.functions.at("eliminate_never_as_argument").transfer, {});
        output << "impossible-proof-argument-result: " << render_stage_value(impossible_argument.result) << '\n';

        constexpr std::string_view mutual_source = R"fine(
enum Nat { zero, succ(Nat) }
function even(value: Nat) -> Bool {
  match value { zero => true, succ(previous) => odd(previous), }
}
function odd(value: Nat) -> Bool {
  match value { zero => false, succ(previous) => even(previous), }
}
function four_even() -> Bool { even(succ(succ(succ(succ(zero))))) }
)fine";
        syntax::ConcreteSyntaxTree mutual_tree = syntax::parse_tree(mutual_source);
        std::ostringstream checked_output;
        ExecutionResult mutual_execution = execute(mutual_tree.ast, checked_output);
        CertifiedValueFlowProgram certified_mutual =
            build_certified_value_flow(mutual_tree.ast, mutual_execution);
        ValueFlowProgram const &mutual = certified_mutual.program();
        StageAnalysisResult mutual_result = cache.analyze(certified_mutual);
        std::size_t even_scc = mutual.function_sccs().at("even");
        output << "mutual-scc-size: " << mutual.sccs()[even_scc].functions.size() << '\n';
        output << "mutual-recursion-certificates: " << mutual_execution.value_recursion_certificates.size() << '\n';
        output << "mutual-certificate-closure: "
               << mutual_execution.value_recursion_certificates.front().call_graphs() << '/'
               << mutual_execution.value_recursion_certificates.front().closure_graphs() << '/'
               << mutual_execution.value_recursion_certificates.front().idempotent_loops() << '\n';
        output << "mutual-recursion-certified: "
               << (certified_mutual.recursion_certified("even") && certified_mutual.recursion_certified("odd")
                       ? "true"
                       : "false")
               << '\n';
        bool bare_permission_rejected = false;
        try {
            StageAnalysisResult bare_mutual = StageAnalysisCache().analyze(mutual);
            (void)evaluate_certified_stage_function(bare_mutual, "even", {});
        }
        catch (std::logic_error const &) {
            bare_permission_rejected = true;
        }
        output << "bare-recursion-permission-rejected: " << (bare_permission_rejected ? "true" : "false") << '\n';
        bool copied_source_rejected = false;
        try {
            syntax::ConcreteSyntaxTree copied_tree = syntax::parse_tree(mutual_source);
            (void)build_certified_value_flow(copied_tree.ast, mutual_execution);
        }
        catch (std::logic_error const &) {
            copied_source_rejected = true;
        }
        output << "copied-source-certificate-rejected: " << (copied_source_rejected ? "true" : "false") << '\n';
        output << "mutual-even-dependencies: " << bits(mutual_result.functions.at("even").result_parameters) << '\n';
        output << "mutual-odd-dependencies: " << bits(mutual_result.functions.at("odd").result_parameters) << '\n';
        FlowType nat_type{syntax::ValueType::Kind::enumeration, "Nat"};
        auto natural = [&](std::size_t value) {
            StageAbstractValue result = stage_constructor(nat_type, "zero", {});
            while (value-- > 0)
                result = stage_constructor(nat_type, "succ", {std::move(result)});
            return result;
        };
        StageEvaluation exact_mutual = evaluate_certified_stage_function(mutual_result, "even", {natural(4)});
        output << "mutual-exact-result: " << render_stage_value(exact_mutual.result) << '\n';
        output << "mutual-exact-recursion-blocked: "
               << (exact_mutual.recursive_call_blocked ? "true" : "false") << '\n';
        StageEvaluation exact_caller = evaluate_certified_stage_function(mutual_result, "four_even", {});
        output << "certified-recursive-callee-result: " << render_stage_value(exact_caller.result) << '\n';
        StageEvaluation mutual_stage =
            evaluate_certified_stage_function(mutual_result, "even", {stage_runtime(nat_type)});
        output << "mutual-runtime-recursion-blocked: " << (mutual_stage.recursive_call_blocked ? "true" : "false")
               << '\n';
        std::size_t cancellation_polls = 0;
        bool cancellation_observed = false;
        try {
            (void)evaluate_certified_stage_function(
                mutual_result, "even", {natural(8)},
                StageEvaluationControl{[&] { return ++cancellation_polls > 12; }});
        }
        catch (StageEvaluationCancelled const &) {
            cancellation_observed = true;
        }
        output << "mutual-exact-cancellation: " << (cancellation_observed ? "true" : "false") << '\n';
        return 0;
    }

}  // namespace fine::stage
