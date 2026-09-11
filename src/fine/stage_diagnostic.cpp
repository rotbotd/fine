#include "stage_diagnostic.h"

#include "runtime.h"
#include "value_flow.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fine::stage {

    namespace {
        ExecutionResult verify_document(syntax::Document const &document, std::ostream &failure_output) {
            std::ostringstream verification;
            try {
                return execute(document, verification);
            }
            catch (...) {
                // Preserve a typed counterexample if ordinary verification emitted
                // one before rejecting the document.
                failure_output << verification.str();
                throw;
            }
        }

        struct CheckedStageTarget {
            syntax::FunctionDecl const *declaration = nullptr;
            StageEvaluation evaluation;
        };

        CheckedStageTarget evaluate_target(syntax::Document const &document, std::string const &function,
                                           std::ostream &failure_output) {
            ExecutionResult execution = verify_document(document, failure_output);

            auto declaration = std::find_if(document.functions.begin(), document.functions.end(),
                                            [&](syntax::FunctionDecl const &item) { return item.name == function; });
            if (declaration == document.functions.end())
                throw std::runtime_error("unknown value function in stage diagnostic: " + function);
            if (!declaration->parameters.empty())
                throw SemanticError(declaration->span,
                                    "stage target `" + function +
                                        "` has parameters; write a nullary wrapper containing the exact call");

            CertifiedValueFlowProgram certified = build_certified_value_flow(document, execution);
            StageAnalysisResult analysis = StageAnalysisCache().analyze(certified);
            return {&*declaration, evaluate_certified_stage_function(analysis, function, {})};
        }
    }  // namespace

    int run_stage_diagnostic(syntax::Document const &document, std::string const &function,
                             std::ostream &output) {
        CheckedStageTarget target = evaluate_target(document, function, output);
        StageEvaluation const &evaluated = target.evaluation;

        output << "stage " << function << " {\n";
        output << "  result: " << render_stage_value(evaluated.result) << ";\n";
        output << "  executable-match-edges: " << evaluated.executable_edges.size() << ";\n";
        for (auto const &edge : evaluated.executable_edges)
            output << "  match-edge: " << edge.function << '#' << edge.match << '.' << edge.arm << " -> "
                   << edge.constructor << ";\n";
        output << "  recursive-call-blocked: " << (evaluated.recursive_call_blocked ? "true" : "false")
               << ";\n}\n";
        return 0;
    }

    std::string materialize_stage_result(syntax::ConcreteSyntaxTree const &tree, std::string const &function,
                                         std::ostream &failure_output) {
        ExecutionResult execution = verify_document(tree.ast, failure_output);
        auto declaration = std::find_if(tree.ast.functions.begin(), tree.ast.functions.end(),
                                        [&](syntax::FunctionDecl const &item) { return item.name == function; });
        if (declaration == tree.ast.functions.end())
            throw std::runtime_error("unknown value function in stage specialization: " + function);

        CertifiedValueFlowProgram certified = build_certified_value_flow(tree.ast, execution);
        ValueFlowFunction const &flow = certified.program().functions().at(function);
        StageAnalysisResult analysis = StageAnalysisCache().analyze(certified);
        std::vector<StageAbstractValue> runtime_arguments;
        for (auto const &type : flow.parameters())
            runtime_arguments.push_back(stage_runtime(type));
        StageFunctionEvaluation evaluated =
            evaluate_certified_stage_function_nodes(analysis, function, runtime_arguments);

        std::string source = tree.render();
        syntax::ConcreteRange body = syntax::ConcreteRange::from_span(declaration->body.span);
        std::vector<Materialization> candidates;
        for (auto const &site : flow.source_sites()) {
            syntax::ConcreteRange range = syntax::ConcreteRange::from_span(site.span);
            if (range.begin < body.begin || range.end > body.end)
                continue;
            auto observation = evaluated.nodes.find(site.flow_node);
            if (observation == evaluated.nodes.end() || observation->second.recursive_call_blocked ||
                observation->second.result.kind != StageAbstractValue::Kind::comptime ||
                !observation->second.result.exact)
                continue;
            std::string replacement = render_stage_exact_value(*observation->second.result.exact);
            if (source.substr(range.begin, range.end - range.begin) == replacement)
                continue;
            candidates.push_back({range, std::move(replacement)});
        }

        // Prefer the outermost exact expression. Its replacement already
        // subsumes every exact descendant and leaves disjoint exact islands in
        // a runtime expression available to the same pass.
        std::sort(candidates.begin(), candidates.end(), [](Materialization const &left,
                                                           Materialization const &right) {
            if (left.range.begin != right.range.begin)
                return left.range.begin < right.range.begin;
            return left.range.end > right.range.end;
        });
        std::vector<Materialization> edits;
        for (auto &candidate : candidates) {
            bool covered = false;
            for (auto const &accepted : edits)
                covered = covered || (accepted.range.begin <= candidate.range.begin &&
                                      candidate.range.end <= accepted.range.end);
            if (!covered)
                edits.push_back(std::move(candidate));
        }
        if (edits.empty() && flow.parameters().empty() &&
            (evaluated.root.result.kind != StageAbstractValue::Kind::comptime || !evaluated.root.result.exact))
            throw SemanticError(declaration->body.span,
                                "stage target `" + function + "` did not produce an exact compile-time value");
        if (edits.empty() && flow.parameters().empty() && evaluated.root.recursive_call_blocked)
            throw SemanticError(declaration->body.span,
                                "stage target `" + function + "` retained a blocked recursive call");
        if (edits.empty())
            throw SemanticError(declaration->body.span,
                                "stage target `" + function + "` has no reducible compile-time expression");

        std::string specialized = apply_materializations(tree, std::move(edits));

        // Reparse, reverify, and re-run the function at the same runtime-input
        // abstraction. Specialization may remove match edges, but it may not
        // change the abstract result or introduce a blocked recursive call.
        syntax::ConcreteSyntaxTree reparsed = syntax::parse_tree(specialized);
        ExecutionResult validation_execution = verify_document(reparsed.ast, failure_output);
        CertifiedValueFlowProgram validation_certified =
            build_certified_value_flow(reparsed.ast, validation_execution);
        StageAnalysisResult validation_analysis = StageAnalysisCache().analyze(validation_certified);
        StageEvaluation validation =
            evaluate_certified_stage_function(validation_analysis, function, runtime_arguments);
        if (validation.result != evaluated.root.result ||
            validation.recursive_call_blocked != evaluated.root.recursive_call_blocked)
            throw std::logic_error("stage specialization changed its abstract result after source reparse");
        return specialized;
    }

}  // namespace fine::stage
