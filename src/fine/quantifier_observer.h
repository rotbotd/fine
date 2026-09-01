#pragma once

#include "c++/z3++.h"

#include <memory>
#include <string>
#include <vector>

namespace fine {

class RainfallRecorder;

// Observes accepted, nontrivial quantifier instances at Z3's existing
// user-propagator binding boundary. It never blocks an instance. The callback
// itself does not identify the upstream instantiation engine; callers may only
// attribute an instance to MBQI when the query disables E-matching.
class RainfallQuantifierObserver final : public z3::user_propagator_base {
public:
    RainfallQuantifierObserver(
        z3::solver& solver, RainfallRecorder& rainfall,
        std::vector<std::string> within, bool ematching_enabled,
        bool mbqi_enabled);

    void push() override;
    void pop(unsigned scopes) override;
    z3::user_propagator_base* fresh(z3::context& context) override;
    bool on_binding(z3::expr const& quantifier,
                    z3::expr const& instance) override;

private:
    explicit RainfallQuantifierObserver(z3::context& context);

    RainfallRecorder* rainfall_ = nullptr;
    std::vector<std::string> within_;
    bool ematching_enabled_ = true;
    bool mbqi_enabled_ = true;
};

} // namespace fine
