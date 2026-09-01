#include "quantifier_observer.h"

#include "rainfall.h"

#include <utility>

namespace fine {

RainfallQuantifierObserver::RainfallQuantifierObserver(
    z3::solver& solver, RainfallRecorder& rainfall,
    std::vector<std::string> within, bool ematching_enabled)
    : z3::user_propagator_base(&solver),
      rainfall_(&rainfall),
      within_(std::move(within)),
      ematching_enabled_(ematching_enabled) {
    register_on_binding();
}

RainfallQuantifierObserver::RainfallQuantifierObserver(z3::context& context)
    : z3::user_propagator_base(context) {}

void RainfallQuantifierObserver::push() {}

void RainfallQuantifierObserver::pop(unsigned) {}

z3::user_propagator_base*
RainfallQuantifierObserver::fresh(z3::context& context) {
    // Fresh internal solver contexts use a different AST manager. Their terms
    // cannot enter the recorder tied to the parent manager. The accepted
    // instance is observed when it reaches the parent qi_queue instead.
    return new RainfallQuantifierObserver(context);
}

bool RainfallQuantifierObserver::on_binding(
    z3::expr const& quantifier, z3::expr const& instance) {
    if (!rainfall_) return true;

    std::string quantifier_reference = rainfall_->term(quantifier);
    std::string instance_reference = rainfall_->term(instance);
    z3::symbol qid(quantifier.ctx(),
                   Z3_get_quantifier_id(quantifier.ctx(), quantifier));
    std::string source_role = qid.kind() == Z3_STRING_SYMBOL
                                  ? qid.str()
                                  : std::string("unlabelled");
    std::string event = rainfall_->record(
        "derive",
        ematching_enabled_ ? "z3.quantifier-instance" : "z3.mbqi-instance",
        within_, "z3.qi_queue.on_binding",
        "Accepted nontrivial quantifier instances after redundancy and rewrite-to-true rejection, before lemma insertion; discarded candidates and auxiliary-context search are not exposed, and MBQI attribution comes from E-matching being disabled for this query",
        {RainfallRecorder::string_field("quantifier", quantifier_reference),
         RainfallRecorder::string_field("instance", instance_reference),
         RainfallRecorder::string_field("source_role", source_role),
         RainfallRecorder::string_field(
             "instantiation_engine",
             ematching_enabled_ ? "not-distinguished" : "mbqi-only-query"),
         RainfallRecorder::boolean_field("ematching", ematching_enabled_),
         RainfallRecorder::string_field(
             "relation", "quantifier-body-under-ground-binding")});
    rainfall_->remember_quantifier_instance(
        std::move(quantifier_reference), std::move(instance_reference),
        std::move(event));
    return true;
}

} // namespace fine
