#include "proof_model_selector.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using fine::proof_model::Grammar;
using fine::proof_model::Production;
using fine::proof_model::ProductionKind;
using fine::proof_model::Type;

struct Score {
    std::size_t cost = 0;
    bool complete = false;
    std::size_t frontier = 0;
    std::size_t opens = 0;
};

struct Key {
    Type type;
    Score score;

    auto tie() const {
        return std::tie(type.carrier, type.left, type.right, score.cost, score.complete,
                        score.frontier, score.opens);
    }
    bool operator<(Key const &other) const { return tie() < other.tie(); }
};

bool same(Type const &left, Type const &right) {
    return left.carrier == right.carrier && left.left == right.left && left.right == right.right;
}

struct Transition {
    std::size_t production = 0;
    std::vector<Key> children;
};

struct State {
    std::vector<Transition> transitions;
    std::optional<z3::sort> sort;
    std::vector<z3::func_decl> constructors;
};

struct Tree {
    Key key;
    std::string source;
};

std::string key_name(Key const &key) {
    std::ostringstream out;
    out << "S-" << key.type.carrier << '-' << key.type.left << '-' << key.type.right << '-'
        << key.score.cost << '-' << key.score.complete << '-' << key.score.frontier << '-'
        << key.score.opens;
    return out.str();
}

Score score(Production const &production, std::vector<Key> const &children) {
    bool open = production.kind == ProductionKind::open;
    Score result{open ? 0u : 1u, !open, open ? 0u : 1u, open ? 1u : 0u};
    for (Key const &child : children) {
        result.cost += child.score.cost;
        result.complete = result.complete && child.score.complete;
        result.opens += child.score.opens;
        result.frontier += child.score.complete ? 1 : child.score.frontier;
    }
    if (!children.empty())
        result.frontier = result.complete ? 1 : result.frontier - 1;
    return result;
}

void child_products(std::vector<std::vector<Key>> const &choices, std::size_t index,
                    std::vector<Key> &current, std::vector<std::vector<Key>> &result) {
    if (index == choices.size()) {
        result.push_back(current);
        return;
    }
    for (Key const &choice : choices[index]) {
        current.push_back(choice);
        child_products(choices, index + 1, current, result);
        current.pop_back();
    }
}

std::string render(Production const &production, std::vector<std::string> const &children) {
    if (production.kind == ProductionKind::open)
        return "?";
    if (production.kind != ProductionKind::application)
        return production.source;
    std::ostringstream out;
    out << production.function << '(';
    for (std::size_t i = 0; i < production.index_arguments.size(); ++i) {
        if (i)
            out << ", ";
        out << production.index_arguments[i];
    }
    out << ')';
    if (!production.coeffects.empty())
        out << " using [";
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (i)
            out << ", ";
        out << production.coeffects.at(i) << " = " << children[i];
    }
    if (!production.coeffects.empty())
        out << ']';
    return out.str();
}

void tree_products(std::vector<std::vector<Tree const *>> const &choices, std::size_t index,
                   std::vector<Tree const *> &current, std::vector<std::vector<Tree const *>> &result) {
    if (index == choices.size()) {
        result.push_back(current);
        return;
    }
    for (Tree const *choice : choices[index]) {
        current.push_back(choice);
        tree_products(choices, index + 1, current, result);
        current.pop_back();
    }
}

std::vector<Tree> enumerate_reference(Grammar const &grammar) {
    std::vector<Tree> trees;
    for (std::size_t cost_limit = 0; cost_limit <= grammar.max_cost; ++cost_limit) {
        std::size_t prior_size = trees.size();
        std::vector<Tree> produced;
        for (Production const &production : grammar.productions) {
            if (production.arguments.empty()) {
                Score leaf_score = score(production, {});
                if (leaf_score.cost == cost_limit)
                    produced.push_back(Tree{Key{production.result, leaf_score}, render(production, {})});
                continue;
            }
            std::vector<std::vector<Tree const *>> choices;
            bool possible = true;
            for (Type const &argument : production.arguments) {
                std::vector<Tree const *> matching;
                for (std::size_t i = 0; i < prior_size; ++i)
                    if (same(trees[i].key.type, argument))
                        matching.push_back(&trees[i]);
                if (matching.empty()) {
                    possible = false;
                    break;
                }
                choices.push_back(std::move(matching));
            }
            if (!possible)
                continue;
            std::vector<std::vector<Tree const *>> combinations;
            std::vector<Tree const *> current;
            tree_products(choices, 0, current, combinations);
            for (auto const &children : combinations) {
                std::vector<Key> child_keys;
                std::vector<std::string> child_sources;
                for (Tree const *child : children) {
                    child_keys.push_back(child->key);
                    child_sources.push_back(child->source);
                }
                Score result_score = score(production, child_keys);
                if (result_score.cost == cost_limit)
                    produced.push_back(
                        Tree{Key{production.result, result_score}, render(production, child_sources)});
            }
        }
        trees.insert(trees.end(), std::make_move_iterator(produced.begin()),
                     std::make_move_iterator(produced.end()));
    }
    return trees;
}

struct DirectGrammar {
    Grammar grammar;
    std::map<Key, State> states;

    explicit DirectGrammar(Grammar value) : grammar(std::move(value)) {}

    void construct_states() {
        for (std::size_t cost_limit = 0; cost_limit <= grammar.max_cost; ++cost_limit) {
            // New states at this cost may only refer to strictly cheaper states because
            // every non-open production contributes one unit of constructor cost.
            std::vector<Key> available;
            for (auto const &[key, _] : states)
                if (key.score.cost < cost_limit)
                    available.push_back(key);

            for (std::size_t production_index = 0; production_index < grammar.productions.size();
                 ++production_index) {
                Production const &production = grammar.productions[production_index];
                std::size_t own_cost = production.kind == ProductionKind::open ? 0 : 1;
                if (own_cost > cost_limit)
                    continue;

                if (production.arguments.empty()) {
                    Score leaf_score = score(production, {});
                    if (leaf_score.cost == cost_limit)
                        states[Key{production.result, leaf_score}].transitions.push_back(
                            Transition{production_index, {}});
                    continue;
                }

                std::vector<std::vector<Key>> choices;
                bool possible = true;
                for (Type const &argument : production.arguments) {
                    std::vector<Key> matching;
                    for (Key const &key : available)
                        if (same(key.type, argument))
                            matching.push_back(key);
                    if (matching.empty()) {
                        possible = false;
                        break;
                    }
                    choices.push_back(std::move(matching));
                }
                if (!possible)
                    continue;

                std::vector<std::vector<Key>> combinations;
                std::vector<Key> current;
                child_products(choices, 0, current, combinations);
                for (auto &children : combinations) {
                    Score result_score = score(production, children);
                    if (result_score.cost == cost_limit)
                        states[Key{production.result, result_score}].transitions.push_back(
                            Transition{production_index, std::move(children)});
                }
            }
        }
    }

    void make_sorts(z3::context &context) {
        for (std::size_t cost = 0; cost <= grammar.max_cost; ++cost) {
            for (auto &[key, state] : states) {
                if (key.score.cost != cost)
                    continue;
                std::string state_name = key_name(key);
                z3::constructors declarations(context);
                for (std::size_t alternative = 0; alternative < state.transitions.size(); ++alternative) {
                    Transition const &transition = state.transitions[alternative];
                    std::vector<z3::symbol> fields;
                    std::vector<z3::sort> sorts;
                    for (std::size_t child = 0; child < transition.children.size(); ++child) {
                        fields.push_back(context.str_symbol(
                            (state_name + "-field-" + std::to_string(alternative) + '-' +
                             std::to_string(child)).c_str()));
                        auto found = states.find(transition.children[child]);
                        if (found == states.end() || !found->second.sort)
                            throw std::logic_error("state grammar referred to a non-earlier child sort");
                        sorts.push_back(*found->second.sort);
                    }
                    std::string constructor =
                        state_name + "-alt-" + std::to_string(alternative) + "-p-" +
                        std::to_string(transition.production);
                    declarations.add(context.str_symbol(constructor.c_str()),
                                     context.str_symbol(("is-" + constructor).c_str()),
                                     static_cast<unsigned>(fields.size()), fields.data(), sorts.data());
                }
                state.sort.emplace(context.datatype(context.str_symbol(state_name.c_str()), declarations));
                z3::func_decl_vector constructors = state.sort->constructors();
                for (unsigned i = 0; i < constructors.size(); ++i)
                    state.constructors.push_back(constructors[i]);
            }
        }
    }

    Key preferred_root() const {
        std::optional<Key> best;
        for (auto const &[key, _] : states) {
            if (!same(key.type, grammar.expected) || key.score.cost > grammar.max_cost)
                continue;
            if (!best || key.score.complete > best->score.complete ||
                (key.score.complete == best->score.complete &&
                 key.score.frontier > best->score.frontier) ||
                (key.score.complete == best->score.complete &&
                 key.score.frontier == best->score.frontier && key.score.cost < best->score.cost))
                best = key;
        }
        if (!best)
            throw std::runtime_error("direct grammar has no root state");
        return *best;
    }

    std::string lift(z3::expr const &value, Key const &key) const {
        State const &state = states.at(key);
        std::size_t alternative = state.transitions.size();
        for (std::size_t i = 0; i < state.constructors.size(); ++i)
            if (value.decl().id() == state.constructors[i].id()) {
                alternative = i;
                break;
            }
        if (alternative == state.transitions.size())
            throw std::runtime_error("model constructor is outside its exact state");
        Transition const &transition = state.transitions[alternative];
        Production const &production = grammar.productions[transition.production];
        if (production.kind == ProductionKind::open)
            return "?";
        if (production.kind != ProductionKind::application)
            return production.source;

        std::vector<std::string> children;
        for (unsigned i = 0; i < value.num_args(); ++i) {
            children.push_back(lift(value.arg(i), transition.children.at(i)));
        }
        return render(production, children);
    }
};

Type type(unsigned left, unsigned right) { return Type{1, left, right}; }

Production open(Type result) {
    Production value;
    value.kind = ProductionKind::open;
    value.source = "?";
    value.result = result;
    return value;
}

Production local(std::string source, Type result) {
    Production value;
    value.kind = ProductionKind::local;
    value.source = std::move(source);
    value.result = result;
    return value;
}

Production application(std::string function, std::vector<std::string> indices,
                       std::vector<std::string> coeffects, Type result, std::vector<Type> arguments) {
    Production value;
    value.kind = ProductionKind::application;
    value.function = std::move(function);
    value.index_arguments = std::move(indices);
    value.coeffects = std::move(coeffects);
    value.result = result;
    value.arguments = std::move(arguments);
    return value;
}

Grammar checkpoint_grammar() {
    constexpr unsigned left = 10;
    constexpr unsigned middle = 11;
    constexpr unsigned right = 12;
    Grammar grammar;
    grammar.id = "direct-spike";
    grammar.expected = type(left, right);
    grammar.max_cost = 2;

    auto name = [&](unsigned value) {
        if (value == left)
            return std::string("left");
        if (value == middle)
            return std::string("middle");
        return std::string("right");
    };

    // This is the production union behind identity-checkpoint.fine's discarded
    // all-frontier encoding, not the eleven concrete trees in that frontier.
    for (unsigned a : {left, middle, right})
        for (unsigned b : {left, middle, right})
            grammar.productions.push_back(open(type(a, b)));
    grammar.productions.push_back(local("p", type(left, middle)));
    for (unsigned a : {left, middle, right})
        for (unsigned b : {left, middle, right})
            grammar.productions.push_back(application(
                "symm", {name(a), name(b)}, {"given"}, type(b, a), {type(a, b)}));
    for (unsigned a : {left, middle, right})
        for (unsigned b : {left, middle, right})
            for (unsigned c : {left, middle, right})
                grammar.productions.push_back(application(
                    "trans", {name(a), name(b), name(c)},
                    {"first", "second"}, type(a, c), {type(a, b), type(b, c)}));
    return grammar;
}

template <typename Function>
auto timed(Function &&function) {
    auto begin = std::chrono::steady_clock::now();
    auto value = function();
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin);
    return std::pair{std::move(value), elapsed.count()};
}

} // namespace

int main() {
    Grammar grammar = checkpoint_grammar();

    std::vector<Tree> reference = enumerate_reference(grammar);

    z3::context old_context;
    auto [old_result, old_ms] = timed([&] { return fine::proof_model::select(old_context, grammar); });
    std::cout << "recursive-functions: "
              << (old_result.status == fine::proof_model::Status::sat ? "sat" :
                  old_result.status == fine::proof_model::Status::unsat ? "unsat" : "unknown")
              << " in " << old_ms << " ms";
    if (!old_result.reason.empty())
        std::cout << " (" << old_result.reason << ')';
    std::cout << '\n';

    z3::context direct_context;
    DirectGrammar direct(grammar);
    auto [ignored, construct_ms] = timed([&] {
        direct.construct_states();
        direct.make_sorts(direct_context);
        return 0;
    });
    (void)ignored;
    Key root = direct.preferred_root();

    std::map<Key, bool> reference_states;
    for (Tree const &tree : reference)
        reference_states[tree.key] = true;
    if (reference_states.size() != direct.states.size())
        throw std::runtime_error("direct grammar and concrete reference disagree on state count");
    for (auto const &[key, _] : direct.states)
        if (!reference_states.contains(key))
            throw std::runtime_error("direct grammar admitted a state outside the concrete reference");
    State const &root_state = direct.states.at(root);
    z3::expr hole = direct_context.constant("direct-hole", *root_state.sort);
    auto [direct_result, solve_ms] = timed([&] {
        z3::solver solver(direct_context);
        z3::check_result status = solver.check();
        if (status != z3::sat)
            throw std::runtime_error("direct state grammar did not solve");
        return solver.get_model().eval(hole, true);
    });

    std::string lifted = direct.lift(direct_result, root);
    bool found_lifted = false;
    for (Tree const &tree : reference)
        if (!(tree.key < root) && !(root < tree.key) && tree.source == lifted)
            found_lifted = true;
    if (!found_lifted)
        throw std::runtime_error("direct model lifted outside the concrete reference frontier");

    std::cout << "state-indexed: sat in " << solve_ms << " ms after " << construct_ms
              << " ms construction\n";
    std::cout << "reference trees: " << reference.size() << ", exact states: "
              << direct.states.size() << ", root alternatives: "
              << root_state.transitions.size() << '\n';
    std::cout << "score: complete=" << root.score.complete << " frontier=" << root.score.frontier
              << " opens=" << root.score.opens << " cost=" << root.score.cost << '\n';
    std::cout << "lifted: " << lifted << '\n';
}
