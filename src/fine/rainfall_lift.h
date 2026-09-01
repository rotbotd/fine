#pragma once

#include "c++/z3++.h"

#include <string>

namespace fine {

    struct RainfallLiftedTerm {
        std::string text;
        std::string sorts_json;
        std::string declarations_json;
    };

    // Canonical generated Fine syntax for a live Z3 expression. The syntax is
    // intentionally declaration-explicit: aliases are accompanied by the exact
    // manager-local sorts and declarations used to reify them. Construction
    // reparses the emitted text and requires exact AST identity before returning.
    RainfallLiftedTerm lift_rainfall_term(z3::context &context, z3::expr const &expression,
                                          bool exact_reify = true);

}  // namespace fine
