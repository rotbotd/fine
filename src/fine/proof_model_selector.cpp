#include "proof_model_selector.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
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

        std::string constructor_base(Production const &production) {
            switch (production.kind) {
            case ProductionKind::open: return "open";
            case ProductionKind::local: return "local-" + symbol_part(production.source);
            case ProductionKind::reflexivity: return "refl-" + symbol_part(production.source);
            case ProductionKind::application: return "apply-" + symbol_part(production.function);
            }
            return "proof";
        }

        z3::expr integer(z3::context &context, unsigned value) {
            return context.int_val(static_cast<std::uint64_t>(value));
        }

        struct Datatype {
            z3::sort sort;
            z3::func_decl_vector constructors;
            z3::func_decl_vector recognizers;

            explicit Datatype(z3::sort value)
                : sort(std::move(value)), constructors(sort.constructors()), recognizers(sort.recognizers()) {}
        };

        std::string lift(z3::expr const &value, Grammar const &grammar, Datatype const &datatype) {
            if (!value.is_app())
                throw std::runtime_error("proof model is not a constructor application");
            std::size_t index = grammar.productions.size();
            for (std::size_t i = 0; i < grammar.productions.size(); ++i) {
                if (value.decl().id() == datatype.constructors[static_cast<unsigned>(i)].id()) {
                    index = i;
                    break;
                }
            }
            if (index == grammar.productions.size())
                throw std::runtime_error("proof model uses a constructor outside the Fine grammar");

            Production const &production = grammar.productions[index];
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

            std::ostringstream source;
            source << production.function;
            if (!production.index_arguments.empty()) {
                source << '[';
                for (std::size_t i = 0; i < production.index_arguments.size(); ++i) {
                    if (i)
                        source << ", ";
                    source << production.index_arguments[i];
                }
                source << ']';
            }
            source << '(';
            for (unsigned i = 0; i < value.num_args(); ++i) {
                if (i)
                    source << ", ";
                source << lift(value.arg(i), grammar, datatype);
            }
            source << ')';
            return source.str();
        }

    }  // namespace

    Result select(z3::context &context, Grammar const &grammar) {
        if (grammar.productions.empty() || grammar.max_cost == 0)
            return {Status::unsat, "empty bounded Fine proof grammar", {}, {}, 0, false, 0, 0};

        std::string suffix = symbol_part(grammar.id);
        std::string sort_name = "FineProof-" + suffix;
        z3::symbol sort_symbol = context.str_symbol(sort_name.c_str());
        z3::sort recursive_sort = context.datatype_sort(sort_symbol);
        z3::constructors declarations(context);
        std::map<std::string, std::size_t> used_names;
        for (std::size_t i = 0; i < grammar.productions.size(); ++i) {
            Production const &production = grammar.productions[i];
            std::string base = constructor_base(production);
            std::size_t ordinal = used_names[base]++;
            std::string name = ordinal == 0 ? base : base + "-" + std::to_string(ordinal);
            std::string recognizer = "is-" + name;
            std::vector<z3::symbol> field_names;
            std::vector<z3::sort> field_sorts;
            for (std::size_t child = 0; child < production.arguments.size(); ++child) {
                field_names.push_back(
                    context.str_symbol(("child-" + std::to_string(i) + "-" + std::to_string(child)).c_str()));
                field_sorts.push_back(recursive_sort);
            }
            declarations.add(context.str_symbol(name.c_str()), context.str_symbol(recognizer.c_str()),
                             static_cast<unsigned>(field_names.size()), field_names.data(), field_sorts.data());
        }
        Datatype datatype(context.datatype(sort_symbol, declarations));

        z3::func_decl source = context.recfun(("fine-proof-src-" + suffix).c_str(), datatype.sort, context.int_sort());
        z3::func_decl destination =
            context.recfun(("fine-proof-dst-" + suffix).c_str(), datatype.sort, context.int_sort());
        z3::func_decl carrier =
            context.recfun(("fine-proof-carrier-" + suffix).c_str(), datatype.sort, context.int_sort());
        z3::func_decl cost = context.recfun(("fine-proof-cost-" + suffix).c_str(), datatype.sort, context.int_sort());
        z3::func_decl complete =
            context.recfun(("fine-proof-complete-" + suffix).c_str(), datatype.sort, context.bool_sort());
        z3::func_decl frontier =
            context.recfun(("fine-proof-frontier-" + suffix).c_str(), datatype.sort, context.int_sort());
        z3::func_decl opens =
            context.recfun(("fine-proof-opens-" + suffix).c_str(), datatype.sort, context.int_sort());
        z3::func_decl well = context.recfun(("fine-proof-well-" + suffix).c_str(), datatype.sort, context.bool_sort());
        z3::expr node = context.constant(("fine-proof-node-" + suffix).c_str(), datatype.sort);

        std::vector<z3::expr> source_cases;
        std::vector<z3::expr> destination_cases;
        std::vector<z3::expr> carrier_cases;
        std::vector<z3::expr> cost_cases;
        std::vector<z3::expr> complete_cases;
        std::vector<z3::expr> frontier_cases;
        std::vector<z3::expr> open_cases;
        std::vector<z3::expr> well_cases;
        for (std::size_t i = 0; i < grammar.productions.size(); ++i) {
            Production const &production = grammar.productions[i];
            source_cases.push_back(integer(context, production.result.left));
            destination_cases.push_back(integer(context, production.result.right));
            carrier_cases.push_back(integer(context, production.result.carrier));
            bool is_open = production.kind == ProductionKind::open;
            z3::expr production_cost = context.int_val(is_open ? 0 : 1);
            z3::expr production_complete = context.bool_val(!is_open);
            z3::expr production_frontier = context.int_val(is_open ? 0 : 1);
            z3::expr production_opens = context.int_val(is_open ? 1 : 0);
            z3::expr production_well = context.bool_val(true);
            z3::func_decl_vector accessors = datatype.constructors[static_cast<unsigned>(i)].accessors();
            for (unsigned child_index = 0; child_index < accessors.size(); ++child_index) {
                z3::expr child = accessors[child_index](node);
                Type const &expected = production.arguments[child_index];
                production_cost = production_cost + cost(child);
                production_complete = production_complete && complete(child);
                production_opens = production_opens + opens(child);
                production_frontier = production_frontier +
                                      z3::ite(complete(child), context.int_val(1), frontier(child));
                production_well = production_well && well(child) &&
                                  carrier(child) == integer(context, expected.carrier) &&
                                  source(child) == integer(context, expected.left) &&
                                  destination(child) == integer(context, expected.right);
            }
            cost_cases.push_back(std::move(production_cost));
            complete_cases.push_back(production_complete);
            frontier_cases.push_back(
                production.arguments.empty() ? production_frontier
                                             : z3::ite(production_complete, context.int_val(1), production_frontier - 1));
            open_cases.push_back(std::move(production_opens));
            well_cases.push_back(std::move(production_well));
        }

        auto cases = [&](std::vector<z3::expr> const &values) {
            z3::expr body = values.back();
            for (std::size_t i = values.size() - 1; i-- > 0;)
                body = z3::ite(datatype.recognizers[static_cast<unsigned>(i)](node), values[i], body);
            return body;
        };
        z3::expr_vector arguments(context);
        arguments.push_back(node);
        context.recdef(source, arguments, cases(source_cases));
        context.recdef(destination, arguments, cases(destination_cases));
        context.recdef(carrier, arguments, cases(carrier_cases));
        context.recdef(cost, arguments, cases(cost_cases));
        context.recdef(complete, arguments, cases(complete_cases));
        context.recdef(frontier, arguments, cases(frontier_cases));
        context.recdef(opens, arguments, cases(open_cases));
        context.recdef(well, arguments, cases(well_cases));

        z3::expr hole = context.constant(("fine-proof-hole-" + suffix).c_str(), datatype.sort);
        z3::solver solver(context);
        z3::params options(context);
#ifdef __EMSCRIPTEN__
        // Emscripten's single-threaded runtime cannot create the timer thread
        // behind Z3's wall-clock timeout. A solver resource limit preserves a
        // bounded browser call without pretending pthreads are available.
        options.set("rlimit", 5000000u);
#else
        options.set("timeout", 5000u);
#endif
        solver.set(options);
        solver.add(well(hole));
        solver.add(carrier(hole) == integer(context, grammar.expected.carrier));
        solver.add(source(hole) == integer(context, grammar.expected.left));
        solver.add(destination(hole) == integer(context, grammar.expected.right));
        solver.add(cost(hole) <= context.int_val(static_cast<std::uint64_t>(grammar.max_cost)));
        solver.add(complete(hole) == context.bool_val(grammar.preferred_complete));
        solver.add(frontier(hole) ==
                   context.int_val(static_cast<std::uint64_t>(grammar.preferred_closed_frontier)));
        solver.add(opens(hole) == context.int_val(static_cast<std::uint64_t>(grammar.preferred_open_leaves)));
        solver.add(cost(hole) == context.int_val(static_cast<std::uint64_t>(grammar.preferred_cost)));

        z3::check_result status = solver.check();
        if (status == z3::unsat)
            return {Status::unsat, "bounded datatype grammar is unsatisfiable", {}, {}, 0, false, 0, 0};
        if (status == z3::unknown)
            return {Status::unknown, solver.reason_unknown(), {}, {}, 0, false, 0, 0};

        z3::model model = solver.get_model();
        z3::expr value = model.eval(hole, true);
        z3::expr cost_value = model.eval(cost(value), true);
        std::uint64_t selected_cost = 0;
        if (!cost_value.is_numeral_u64(selected_cost))
            throw std::runtime_error("proof model cost did not evaluate to a numeral");
        z3::expr complete_value = model.eval(complete(value), true);
        z3::expr frontier_value = model.eval(frontier(value), true);
        z3::expr opens_value = model.eval(opens(value), true);
        std::uint64_t selected_frontier = 0;
        std::uint64_t selected_opens = 0;
        if (!frontier_value.is_numeral_u64(selected_frontier) || !opens_value.is_numeral_u64(selected_opens))
            throw std::runtime_error("partial proof model scores did not evaluate to numerals");
        return {Status::sat,
                {},
                value.to_string(),
                lift(value, grammar, datatype),
                static_cast<std::size_t>(selected_cost),
                complete_value.is_true(),
                static_cast<std::size_t>(selected_frontier),
                static_cast<std::size_t>(selected_opens)};
    }

}  // namespace fine::proof_model
