#include "stage_diagnostic.h"

#include "runtime.h"
#include "value_flow.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fine::stage {

    int run_stage_diagnostic(syntax::Document const &document, std::string const &function,
                             std::ostream &output) {
        std::ostringstream verification;
        ExecutionResult execution;
        try {
            execution = execute(document, verification);
        }
        catch (...) {
            // Preserve a typed counterexample if ordinary verification emitted
            // one before rejecting the document.
            output << verification.str();
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
        StageEvaluation evaluated = evaluate_certified_stage_function(analysis, function, {});

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

}  // namespace fine::stage
