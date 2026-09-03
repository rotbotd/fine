#pragma once

#include "c++/z3++.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fine::proof_model {

    struct Type {
        unsigned carrier = 0;
        unsigned left = 0;
        unsigned right = 0;
    };

    enum class ProductionKind { open, local, reflexivity, application };

    struct Production {
        ProductionKind kind = ProductionKind::local;
        std::string source;
        std::string function;
        std::vector<std::string> index_arguments;
        Type result;
        std::vector<Type> arguments;
    };

    struct Grammar {
        std::string id;
        Type expected;
        std::size_t max_cost = 0;
        bool preferred_complete = true;
        std::size_t preferred_closed_frontier = 1;
        std::size_t preferred_open_leaves = 0;
        std::size_t preferred_cost = 0;
        std::string preferred_source;
        std::vector<Production> productions;
    };

    enum class Status { sat, unsat, unknown };

    struct Result {
        Status status = Status::unknown;
        std::string reason;
        std::string model_value;
        std::string source;
        std::size_t cost = 0;
        bool complete = false;
        std::size_t closed_frontier = 0;
        std::size_t open_leaves = 0;
    };

    using Observer =
        std::function<void(Grammar const &, z3::context &, z3::expr const &, Result const &)>;

    std::string lift_model_term(z3::context &context, z3::expr const &value, Grammar const &grammar);
    Result select(z3::context &context, Grammar const &grammar, Observer observer = {});

}  // namespace fine::proof_model
