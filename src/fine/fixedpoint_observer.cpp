#include "fixedpoint_observer.h"

#include "rainfall.h"
#include "z3_fixedpoint.h"

#include <utility>

namespace fine {

RainfallFixedpointObserver::RainfallFixedpointObserver(
    z3::fixedpoint& fixedpoint, RainfallRecorder& rainfall,
    std::vector<std::string> within)
    : context_(fixedpoint.ctx()), rainfall_(rainfall), within_(std::move(within)) {
    Z3_fixedpoint_add_callback(context_, fixedpoint, this, &on_lemma,
                               &on_predecessor, &on_unfold);
    context_.check_error();
}

void RainfallFixedpointObserver::rethrow_if_failed() {
    if (failure_) std::rethrow_exception(failure_);
}

void RainfallFixedpointObserver::on_lemma(void* state, Z3_ast lemma,
                                          unsigned level) noexcept {
    auto& observer = *static_cast<RainfallFixedpointObserver*>(state);
    observer.guarded([&] {
        z3::expr expression(observer.context_, lemma);
        observer.rainfall_.record(
            "derive", "z3.spacer.lemma-export", observer.within_,
            "z3.fixedpoint.new-lemma-callback",
            "Spacer lemma exported through the public fixedpoint callback after addition or duplicate encounter; duplicates are possible and export does not establish causal contribution to the final result",
            {RainfallRecorder::string_field(
                 "lemma", observer.rainfall_.term(expression)),
             RainfallRecorder::number_field("level", level),
             RainfallRecorder::string_field(
                 "relation", "learned-constraint-on-relation")});
    });
}

void RainfallFixedpointObserver::on_predecessor(void* state) noexcept {
    auto& observer = *static_cast<RainfallFixedpointObserver*>(state);
    observer.guarded([&] {
        ++observer.predecessor_count_;
        observer.rainfall_.record(
            "transition", "z3.spacer.predecessor", observer.within_,
            "z3.fixedpoint.predecessor-callback",
            "Spacer crossed its public predecessor-search callback; the callback carries no rule, term, or success payload",
            {RainfallRecorder::number_field("ordinal",
                                             observer.predecessor_count_)});
    });
}

void RainfallFixedpointObserver::on_unfold(void* state) noexcept {
    auto& observer = *static_cast<RainfallFixedpointObserver*>(state);
    observer.guarded([&] {
        ++observer.unfold_count_;
        observer.rainfall_.record(
            "transition", "z3.spacer.unfold", observer.within_,
            "z3.fixedpoint.unfold-callback",
            "Spacer crossed its public unfold callback; the callback carries no relation, rule, or generated obligation",
            {RainfallRecorder::number_field("ordinal", observer.unfold_count_)});
    });
}

} // namespace fine
