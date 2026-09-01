#pragma once

#include "c++/z3++.h"

#include <exception>
#include <string>
#include <vector>

namespace fine {

class RainfallRecorder;

// Observes the three callbacks Z3's public fixedpoint API exports from Spacer.
// Lemmas carry exact terms and levels; predecessor/unfold callbacks expose only
// that the corresponding search boundary was crossed. None is causal evidence
// for the final fixedpoint result, and the callbacks do not expose rule matches.
class RainfallFixedpointObserver {
public:
    RainfallFixedpointObserver(z3::fixedpoint& fixedpoint,
                               RainfallRecorder& rainfall,
                               std::vector<std::string> within);

    void rethrow_if_failed();

private:
    static void on_lemma(void* state, Z3_ast lemma, unsigned level) noexcept;
    static void on_predecessor(void* state) noexcept;
    static void on_unfold(void* state) noexcept;

    template <typename Callback>
    void guarded(Callback&& callback) noexcept {
        if (failure_) return;
        try {
            callback();
        }
        catch (...) {
            failure_ = std::current_exception();
        }
    }

    z3::context& context_;
    RainfallRecorder& rainfall_;
    std::vector<std::string> within_;
    std::exception_ptr failure_;
    unsigned predecessor_count_ = 0;
    unsigned unfold_count_ = 0;
};

} // namespace fine
