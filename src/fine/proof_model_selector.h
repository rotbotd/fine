#pragma once

#include "c++/z3++.h"

#include <cstddef>
#include <functional>
#include <memory>
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
        std::vector<std::string> coeffects;
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
        bool rank_automatically = false;
        bool retain_state_graph = false;
        std::vector<Production> productions;
    };

    enum class Status { sat, unsat, unknown };

    struct TransitionSummary {
        std::size_t production = 0;
        std::vector<std::string> children;
    };

    struct StateSummary {
        std::string id;
        Type type;
        std::size_t cost = 0;
        bool complete = false;
        std::size_t closed_frontier = 0;
        std::size_t open_leaves = 0;
        std::vector<TransitionSummary> transitions;
    };

    struct Result {
        Status status = Status::unknown;
        std::string reason;
        std::string model_value;
        std::string source;
        std::size_t cost = 0;
        bool complete = false;
        std::size_t closed_frontier = 0;
        std::size_t open_leaves = 0;
        std::size_t state_count = 0;
        std::size_t transition_count = 0;
        std::size_t reused_state_count = 0;
        bool state_grammar_reset = false;
        std::size_t root_production = 0;
        std::vector<std::string> root_children;
        std::string root_state;
        std::vector<StateSummary> state_graph;
    };

    using Observer = std::function<void(Grammar const &, z3::context &, z3::expr const &, Result const &)>;

    // Owns datatype sorts in one Z3 context and must not outlive that context.
    class IncrementalSelector {
    public:
        IncrementalSelector();
        ~IncrementalSelector();
        IncrementalSelector(IncrementalSelector &&) noexcept;
        IncrementalSelector &operator=(IncrementalSelector &&) noexcept;
        IncrementalSelector(IncrementalSelector const &) = delete;
        IncrementalSelector &operator=(IncrementalSelector const &) = delete;

        Result select(z3::context &context, Grammar const &grammar, Observer observer = {});

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    std::string lift_model_term(z3::context &context, z3::expr const &value, Grammar const &grammar);
    Result select(z3::context &context, Grammar const &grammar, Observer observer = {});

}  // namespace fine::proof_model
