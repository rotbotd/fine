#pragma once

#include "c++/z3++.h"

#include <cstddef>
#include <string>
#include <vector>

namespace fine::proof_model {

    struct Type {
        unsigned carrier = 0;
        unsigned left = 0;
        unsigned right = 0;
    };

    enum class ProductionKind { local, reflexivity, application };

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
        std::vector<Production> productions;
    };

    enum class Status { sat, unsat, unknown };

    struct Result {
        Status status = Status::unknown;
        std::string reason;
        std::string model_value;
        std::string source;
        std::size_t cost = 0;
    };

    Result select(z3::context &context, Grammar const &grammar);

}  // namespace fine::proof_model
