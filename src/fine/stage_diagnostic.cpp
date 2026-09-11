#include "stage_diagnostic.h"

#include "runtime.h"
#include "value_flow.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fine::stage {

    namespace {
        struct CheckedStageTarget {
            syntax::FunctionDecl const *declaration = nullptr;
            StageEvaluation evaluation;
        };

        CheckedStageTarget evaluate_target(syntax::Document const &document, std::string const &function,
                                           std::ostream &failure_output) {
            std::ostringstream verification;
            ExecutionResult execution;
            try {
                execution = execute(document, verification);
            }
            catch (...) {
                // Preserve a typed counterexample if ordinary verification emitted
                // one before rejecting the document.
                failure_output << verification.str();
                throw;
            }

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
        CheckedStageTarget target = evaluate_target(tree.ast, function, failure_output);
        if (target.evaluation.result.kind != StageAbstractValue::Kind::comptime ||
            !target.evaluation.result.exact)
            throw SemanticError(target.declaration->body.span,
                                "stage target `" + function + "` did not produce an exact compile-time value");
        if (target.evaluation.recursive_call_blocked)
            throw SemanticError(target.declaration->body.span,
                                "stage target `" + function + "` retained a blocked recursive call");

        std::string replacement = render_stage_exact_value(*target.evaluation.result.exact);
        std::string specialized = apply_materializations(
            tree, {{syntax::ConcreteRange::from_span(target.declaration->body.span), std::move(replacement)}});

        // The edit is useful only if the new source remains accepted and its
        // wrapper evaluates to the same Fine-owned exact value.
        syntax::ConcreteSyntaxTree reparsed = syntax::parse_tree(specialized);
        CheckedStageTarget validation = evaluate_target(reparsed.ast, function, failure_output);
        if (validation.evaluation.result != target.evaluation.result ||
            validation.evaluation.recursive_call_blocked)
            throw std::logic_error("stage specialization changed its exact result after source reparse");
        return specialized;
    }

}  // namespace fine::stage
