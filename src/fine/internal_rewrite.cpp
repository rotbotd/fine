#include "internal_rewrite.h"
#include "rainfall.h"

#include "api/api_context.h"
#include "ast/rewriter/th_rewriter.h"

#include <sstream>
#include <utility>

namespace fine {
namespace {

class RainfallTheoryObserver final : public th_rewriter_observer {
public:
    RainfallTheoryObserver(z3::context& context, RainfallRecorder& rainfall,
                           std::vector<std::string> within)
        : context_(context), rainfall_(rainfall), within_(std::move(within)) {}

    void on_rewrite(ast_manager& manager, app* before, expr* after,
                    br_status status) override {
        z3::expr public_before(context_, of_ast(before));
        z3::expr public_after(context_, of_ast(after));
        func_decl* declaration = before->get_decl();
        family_id family = declaration->get_family_id();
        std::string family_name = family == null_family_id
                                      ? "uninterpreted"
                                      : manager.get_family_name(family).str();
        std::ostringstream status_text;
        status_text << status;
        rainfall_.record(
            "transform", "z3.theory-rewrite", within_,
            "z3.th_rewriter.reduce_app",
            "Successful builtin theory application reductions after child rewriting; excludes substitutions, variables, quantifiers, other rewriter instantiations, and solver search",
            {RainfallRecorder::string_field("before",
                 rainfall_.term(public_before)),
             RainfallRecorder::string_field("after",
                 rainfall_.term(public_after)),
             RainfallRecorder::string_field("family", family_name),
             RainfallRecorder::string_field(
                 "declaration", declaration->get_name().str()),
             RainfallRecorder::string_field("continuation", status_text.str()),
             RainfallRecorder::string_field("relation", "theory-equivalent")});
    }

private:
    z3::context& context_;
    RainfallRecorder& rainfall_;
    std::vector<std::string> within_;
};

} // namespace

z3::expr simplify_with_rainfall(
    z3::expr const& expression, RainfallRecorder* rainfall,
    std::vector<std::string> const& within) {
    if (!rainfall) return expression.simplify();

    z3::context& public_context = expression.ctx();
    api::context* api_context = mk_c(public_context);
    ast_manager& manager = api_context->m();
    th_rewriter rewriter(manager);
    RainfallTheoryObserver observer(public_context, *rainfall, within);
    rewriter.set_observer(&observer);

    expr_ref result(manager);
    rewriter(to_expr(static_cast<Z3_ast>(expression)), result);
    return z3::expr(public_context, of_ast(result.get()));
}

} // namespace fine
