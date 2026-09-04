#include "proof_model_selector.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fine::proof_model {
    namespace {
        std::string symbol_part(std::string_view value) {
            std::string result;
            for (char character : value) {
                unsigned char byte = static_cast<unsigned char>(character);
                result.push_back(std::isalnum(byte) ? character : '-');
            }
            if (result.empty())
                result = "anonymous";
            return result;
        }

        bool same_type(Type const &left, Type const &right) {
            return left.carrier == right.carrier && left.left == right.left && left.right == right.right;
        }

        struct Score {
            std::size_t cost = 0;
            bool complete = false;
            std::size_t closed_frontier = 0;
            std::size_t open_leaves = 0;
        };

        struct StateKey {
            Type type;
            Score score;

            auto tie() const {
                return std::tie(type.carrier, type.left, type.right, score.cost, score.complete, score.closed_frontier,
                                score.open_leaves);
            }
            bool operator<(StateKey const &other) const {
                return tie() < other.tie();
            }
        };

        struct Transition {
            std::size_t production = 0;
            std::vector<StateKey> children;
        };

        struct State {
            std::vector<Transition> transitions;
            std::optional<z3::sort> sort;
            std::vector<z3::func_decl> constructors;
            std::vector<z3::func_decl> recognizers;
            std::vector<z3::func_decl_vector> accessors;
        };

        Score production_score(Production const &production, std::vector<StateKey> const &children) {
            bool is_open = production.kind == ProductionKind::open;
            Score result{is_open ? 0u : 1u, !is_open, is_open ? 0u : 1u, is_open ? 1u : 0u};
            for (StateKey const &child : children) {
                result.cost += child.score.cost;
                result.complete = result.complete && child.score.complete;
                result.open_leaves += child.score.open_leaves;
                result.closed_frontier += child.score.complete ? 1 : child.score.closed_frontier;
            }
            if (!children.empty())
                result.closed_frontier = result.complete ? 1 : result.closed_frontier - 1;
            return result;
        }

        void child_state_products(std::vector<std::vector<StateKey>> const &choices, std::size_t index,
                                  std::vector<StateKey> &current, std::vector<std::vector<StateKey>> &result) {
            if (index == choices.size()) {
                result.push_back(current);
                return;
            }
            for (StateKey const &choice : choices[index]) {
                current.push_back(choice);
                child_state_products(choices, index + 1, current, result);
                current.pop_back();
            }
        }

        std::string state_name(std::string const &suffix, StateKey const &key) {
            std::ostringstream result;
            result << "FineProofState-" << suffix << '-' << key.type.carrier << '-' << key.type.left << '-'
                   << key.type.right << '-' << key.score.cost << '-' << key.score.complete << '-'
                   << key.score.closed_frontier << '-' << key.score.open_leaves;
            return result.str();
        }

        std::string constructor_prefix(Grammar const &grammar) {
            return "FineProofStateConstructor-" + symbol_part(grammar.id) + "-p";
        }

        std::optional<std::size_t> constructor_production(std::string_view name, Grammar const &grammar) {
            std::string prefix = constructor_prefix(grammar);
            if (!name.starts_with(prefix))
                return std::nullopt;
            name.remove_prefix(prefix.size());
            std::size_t result = 0;
            auto parsed = std::from_chars(name.data(), name.data() + name.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr == name.data() ||
                !std::string_view(parsed.ptr, static_cast<std::size_t>(name.data() + name.size() - parsed.ptr))
                     .starts_with("-a"))
                return std::nullopt;
            return result;
        }

        std::string render(z3::expr const &value, Grammar const &grammar) {
            if (!value.is_app())
                throw std::runtime_error("proof model is not a constructor application");
            auto index = constructor_production(value.decl().name().str(), grammar);
            if (!index || *index >= grammar.productions.size())
                throw std::runtime_error("proof model uses a constructor outside the Fine grammar");
            Production const &production = grammar.productions[*index];
            if (production.kind == ProductionKind::open) {
                if (value.num_args() != 0)
                    throw std::runtime_error("open proof model has unexpected children");
                return "?";
            }
            if (production.kind != ProductionKind::application) {
                if (value.num_args() != 0)
                    throw std::runtime_error("proof leaf model has unexpected children");
                return production.source;
            }
            if (value.num_args() != production.arguments.size())
                throw std::runtime_error("proof application model has the wrong child arity");
            if (production.coeffects.size() != production.arguments.size())
                throw std::runtime_error("proof application model lost its named coeffects");

            std::ostringstream source;
            source << production.function << '(';
            for (std::size_t i = 0; i < production.index_arguments.size(); ++i) {
                if (i)
                    source << ", ";
                source << production.index_arguments[i];
            }
            source << ')';
            if (!production.coeffects.empty())
                source << " using [";
            for (unsigned i = 0; i < value.num_args(); ++i) {
                if (i)
                    source << ", ";
                source << production.coeffects.at(i) << " = " << render(value.arg(i), grammar);
            }
            if (!production.coeffects.empty())
                source << ']';
            return source.str();
        }

        class StateGrammar {
        public:
            explicit StateGrammar(Grammar const &grammar) : grammar_(grammar) {
                construct_states();
            }

            std::optional<StateKey> preferred() const {
                if (grammar_.rank_automatically) {
                    std::optional<StateKey> best;
                    for (auto const &[key, _] : states_) {
                        if (!same_type(key.type, grammar_.expected) || key.score.cost > grammar_.max_cost)
                            continue;
                        if (!best || key.score.complete > best->score.complete ||
                            (key.score.complete == best->score.complete &&
                             key.score.closed_frontier > best->score.closed_frontier) ||
                            (key.score.complete == best->score.complete &&
                             key.score.closed_frontier == best->score.closed_frontier &&
                             key.score.cost < best->score.cost))
                            best = key;
                    }
                    return best;
                }
                StateKey key{grammar_.expected,
                             {grammar_.preferred_cost, grammar_.preferred_complete, grammar_.preferred_closed_frontier,
                              grammar_.preferred_open_leaves}};
                if (!states_.contains(key))
                    throw std::runtime_error("preferred proof score is absent from the bounded state grammar");
                return key;
            }

            void make_sorts(z3::context &context) {
                std::string suffix = symbol_part(grammar_.id);
                std::size_t alternative_id = 0;
                for (std::size_t cost = 0; cost <= grammar_.max_cost; ++cost) {
                    for (auto &[key, state] : states_) {
                        if (key.score.cost != cost)
                            continue;
                        std::string name = state_name(suffix, key);
                        z3::constructors declarations(context);
                        for (std::size_t alternative = 0; alternative < state.transitions.size(); ++alternative) {
                            Transition const &transition = state.transitions[alternative];
                            std::vector<z3::symbol> fields;
                            std::vector<z3::sort> sorts;
                            for (std::size_t child = 0; child < transition.children.size(); ++child) {
                                fields.push_back(context.str_symbol(
                                    (name + "-field-" + std::to_string(alternative) + '-' + std::to_string(child))
                                        .c_str()));
                                auto found = states_.find(transition.children[child]);
                                if (found == states_.end() || !found->second.sort)
                                    throw std::logic_error("bounded proof state referred to a non-earlier child sort");
                                sorts.push_back(*found->second.sort);
                            }
                            std::string constructor = constructor_prefix(grammar_) +
                                                      std::to_string(transition.production) + "-a" +
                                                      std::to_string(alternative_id++);
                            declarations.add(context.str_symbol(constructor.c_str()),
                                             context.str_symbol(("is-" + constructor).c_str()),
                                             static_cast<unsigned>(fields.size()), fields.data(), sorts.data());
                        }
                        state.sort.emplace(context.datatype(context.str_symbol(name.c_str()), declarations));
                        z3::func_decl_vector constructors = state.sort->constructors();
                        z3::func_decl_vector recognizers = state.sort->recognizers();
                        for (unsigned i = 0; i < constructors.size(); ++i) {
                            state.constructors.push_back(constructors[i]);
                            state.recognizers.push_back(recognizers[i]);
                            state.accessors.push_back(constructors[i].accessors());
                        }
                    }
                }
            }

            z3::sort const &sort(StateKey const &key) const {
                return *states_.at(key).sort;
            }

            std::size_t state_count() const {
                return states_.size();
            }

            std::size_t transition_count() const {
                std::size_t result = 0;
                for (auto const &[_, state] : states_)
                    result += state.transitions.size();
                return result;
            }

            void constrain_first(z3::solver &solver, z3::expr const &value, StateKey const &key) const {
                State const &state = states_.at(key);
                if (state.constructors.empty())
                    throw std::logic_error("bounded proof state has no constructors");
                solver.add(state.recognizers[0](value));
                Transition const &transition = state.transitions[0];
                for (unsigned i = 0; i < transition.children.size(); ++i)
                    constrain_first(solver, state.accessors[0][i](value), transition.children[i]);
            }

        private:
            Grammar const &grammar_;
            std::map<StateKey, State> states_;

            void construct_states() {
                for (std::size_t cost_limit = 0; cost_limit <= grammar_.max_cost; ++cost_limit) {
                    std::vector<StateKey> cheaper;
                    for (auto const &[key, _] : states_)
                        if (key.score.cost < cost_limit)
                            cheaper.push_back(key);

                    for (std::size_t production_index = 0; production_index < grammar_.productions.size();
                         ++production_index) {
                        Production const &production = grammar_.productions[production_index];
                        if (production.kind == ProductionKind::open && !production.arguments.empty())
                            throw std::runtime_error("open proof production has children");
                        std::size_t own_cost = production.kind == ProductionKind::open ? 0 : 1;
                        if (own_cost > cost_limit)
                            continue;

                        if (production.arguments.empty()) {
                            Score score = production_score(production, {});
                            if (score.cost == cost_limit)
                                states_[StateKey{production.result, score}].transitions.push_back(
                                    Transition{production_index, {}});
                            continue;
                        }

                        std::vector<std::vector<StateKey>> choices;
                        bool possible = true;
                        for (Type const &argument : production.arguments) {
                            std::vector<StateKey> matching;
                            for (StateKey const &key : cheaper)
                                if (same_type(key.type, argument))
                                    matching.push_back(key);
                            if (matching.empty()) {
                                possible = false;
                                break;
                            }
                            choices.push_back(std::move(matching));
                        }
                        if (!possible)
                            continue;

                        std::vector<std::vector<StateKey>> combinations;
                        std::vector<StateKey> current;
                        child_state_products(choices, 0, current, combinations);
                        for (auto &children : combinations) {
                            Score score = production_score(production, children);
                            if (score.cost == cost_limit)
                                states_[StateKey{production.result, score}].transitions.push_back(
                                    Transition{production_index, std::move(children)});
                        }
                    }
                }
            }
        };
    }  // namespace

    std::string lift_model_term(z3::context &, z3::expr const &value, Grammar const &grammar) {
        return render(value, grammar);
    }

    Result select(z3::context &context, Grammar const &grammar, Observer observer) {
        if (grammar.productions.empty())
            return {Status::unsat, "empty bounded Fine proof grammar", {}, {}, 0, false, 0, 0};

        StateGrammar states(grammar);
        std::optional<StateKey> preferred = states.preferred();
        if (!preferred)
            return {Status::unsat, "bounded state grammar has no root production", {}, {}, 0, false, 0, 0};
        states.make_sorts(context);
        z3::expr hole =
            context.constant(("fine-proof-hole-" + symbol_part(grammar.id)).c_str(), states.sort(*preferred));
        z3::solver solver(context);
        z3::params options(context);
#ifdef __EMSCRIPTEN__
        options.set("rlimit", 5000000u);
#else
        options.set("timeout", 5000u);
#endif
        solver.set(options);
        states.constrain_first(solver, hole, *preferred);

        z3::check_result status = solver.check();
        if (status == z3::unsat)
            return {Status::unsat, "bounded state grammar is unsatisfiable", {}, {}, 0, false, 0, 0};
        if (status == z3::unknown)
            return {Status::unknown, solver.reason_unknown(), {}, {}, 0, false, 0, 0};

        z3::model model = solver.get_model();
        z3::expr value = model.eval(hole, true);
        Result result{Status::sat,
                      {},
                      value.to_string(),
                      render(value, grammar),
                      preferred->score.cost,
                      preferred->score.complete,
                      preferred->score.closed_frontier,
                      preferred->score.open_leaves,
                      states.state_count(),
                      states.transition_count()};
        if (observer)
            observer(grammar, context, value, result);
        return result;
    }
}  // namespace fine::proof_model
