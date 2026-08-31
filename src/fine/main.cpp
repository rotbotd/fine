#include "c++/z3++.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fine {

using z3::expr;
using z3::func_decl;
using z3::sort;

struct Declarations {
    z3::context& ctx;
    sort left_sort;
    sort right_sort;
    std::array<func_decl, 2> left_case;
    std::array<func_decl, 2> right_case;
    func_decl pair;
    sort pair_sort;

    explicit Declarations(z3::context& c)
        : ctx(c),
          left_sort(c),
          right_sort(c),
          left_case{func_decl(c), func_decl(c)},
          right_case{func_decl(c), func_decl(c)},
          pair(c),
          pair_sort(c) {
        left_sort = make_enum(c, "LeftState", {"left-0", "left-1"}, left_case);
        right_sort = make_enum(c, "RightState", {"right-0", "right-1"}, right_case);
        pair = make_pair(c, left_sort, right_sort);
        pair_sort = pair.range();
    }

private:
    static sort make_enum(z3::context& ctx,
                          char const* name,
                          std::array<char const*, 2> names,
                          std::array<func_decl, 2>& cases) {
        z3::func_decl_vector constructors(ctx);
        z3::func_decl_vector testers(ctx);
        sort result = ctx.enumeration_sort(name, names.size(), names.data(), constructors, testers);
        cases = {constructors[0], constructors[1]};
        return result;
    }

    static func_decl make_pair(z3::context& ctx, sort const& left, sort const& right) {
        char const* field_names[] = {"left", "right"};
        sort fields[] = {left, right};
        z3::func_decl_vector projections(ctx);
        return ctx.tuple_sort("StatePair", 2, field_names, fields, projections);
    }
};

struct PairCase {
    unsigned left;
    unsigned right;
};

struct TableWrite {
    PairCase key;
    bool value;
};

// This is the admitted Fine syntax for the first vertical slice. It owns no
// parallel type universe: resolution goes back through the declarations whose
// sorts and constructors are interned by this Z3 context.
struct TableSyntax {
    bool default_value;
    std::vector<TableWrite> writes;
};

static expr enum_value(std::array<func_decl, 2> const& cases, unsigned index) {
    if (index >= cases.size())
        throw std::runtime_error("enum case outside its finite sort");
    return cases[index]();
}

static expr pair_value(Declarations const& env, PairCase key) {
    return env.pair(enum_value(env.left_case, key.left),
                    enum_value(env.right_case, key.right));
}

static expr reify(Declarations const& env, TableSyntax const& table) {
    expr result = z3::const_array(env.pair_sort, env.ctx.bool_val(table.default_value));
    for (TableWrite const& write : table.writes)
        result = z3::store(result, pair_value(env, write.key), env.ctx.bool_val(write.value));
    return result;
}

static bool bool_literal(expr const& value) {
    if (value.is_true()) return true;
    if (value.is_false()) return false;
    throw std::runtime_error("Fine table contains a non-literal Boolean value");
}

static unsigned enum_case(expr const& value, std::array<func_decl, 2> const& cases) {
    for (unsigned i = 0; i < cases.size(); ++i) {
        expr candidate = cases[i]();
        if (Z3_is_eq_ast(value.ctx(), value, candidate)) return i;
    }
    throw std::runtime_error("Fine table key is not an admitted enum constructor");
}

static PairCase lift_pair(Declarations const& env, expr const& value) {
    if (!value.is_app() || value.num_args() != 2 ||
        !Z3_is_eq_func_decl(value.ctx(), value.decl(), env.pair))
        throw std::runtime_error("Fine table index is not the declared state pair");
    return {enum_case(value.arg(0), env.left_case),
            enum_case(value.arg(1), env.right_case)};
}

static void lift_array(Declarations const& env, expr const& value, TableSyntax& out) {
    if (!value.is_app())
        throw std::runtime_error("Fine table is not an array application");

    switch (value.decl().decl_kind()) {
    case Z3_OP_CONST_ARRAY:
        if (!out.writes.empty())
            throw std::runtime_error("malformed Fine table store chain");
        out.default_value = bool_literal(value.arg(0));
        return;
    case Z3_OP_STORE:
        lift_array(env, value.arg(0), out);
        out.writes.push_back({lift_pair(env, value.arg(1)), bool_literal(value.arg(2))});
        return;
    default:
        throw std::runtime_error("array value is outside Fine's admitted table syntax");
    }
}

static TableSyntax lift(Declarations const& env, expr const& value) {
    TableSyntax result{false, {}};
    lift_array(env, value, result);
    return result;
}

static char const* left_name(unsigned index) {
    static char const* names[] = {"left-0", "left-1"};
    return names[index];
}

static char const* right_name(unsigned index) {
    static char const* names[] = {"right-0", "right-1"};
    return names[index];
}

static void print_table(TableSyntax const& table) {
    std::cout << "let bisim = table(default: " << (table.default_value ? "true" : "false") << ") {\n";
    for (TableWrite const& write : table.writes) {
        std::cout << "  (" << left_name(write.key.left) << ", "
                  << right_name(write.key.right) << "): "
                  << (write.value ? "true" : "false") << ",\n";
    }
    std::cout << "};\n";
}

static int demo_bisim() {
    z3::context ctx;
    Declarations env(ctx);

    expr left0 = env.left_case[0]();
    expr left1 = env.left_case[1]();
    expr right0 = env.right_case[0]();
    expr right1 = env.right_case[1]();
    sort relation_sort = ctx.array_sort(env.pair_sort, ctx.bool_sort());
    expr bisim = ctx.constant("bisim", relation_sort);

    auto related = [&](expr const& left, expr const& right) {
        return z3::select(bisim, env.pair(left, right));
    };
    auto left_label = [&](expr const& state) { return state == left1; };
    auto right_label = [&](expr const& state) { return state == right1; };
    auto left_step = [&](expr const& from, expr const& to) {
        return to == z3::ite(from == left0, left1, left0);
    };
    auto right_step = [&](expr const& from, expr const& to) {
        return to == z3::ite(from == right0, right1, right0);
    };

    z3::solver solver(ctx);
    z3::params parameters(ctx);
    parameters.set("mbqi", true);
    solver.set(parameters);

    expr left = ctx.constant("left-state", env.left_sort);
    expr right = ctx.constant("right-state", env.right_sort);
    expr left_next = ctx.constant("left-next", env.left_sort);
    expr right_next = ctx.constant("right-next", env.right_sort);

    solver.add(z3::forall(left, right,
        z3::implies(related(left, right), left_label(left) == right_label(right))));
    solver.add(z3::forall(left, right, left_next,
        z3::implies(related(left, right) && left_step(left, left_next),
                    z3::exists(right_next,
                        right_step(right, right_next) && related(left_next, right_next)))));
    solver.add(z3::forall(left, right, right_next,
        z3::implies(related(left, right) && right_step(right, right_next),
                    z3::exists(left_next,
                        left_step(left, left_next) && related(left_next, right_next)))));
    solver.add(related(left0, right0));

    if (solver.check() != z3::sat) {
        std::cerr << "Fine expected the two-state bisimulation hole to be satisfiable\n";
        return EXIT_FAILURE;
    }

    z3::model model = solver.get_model();
    TableSyntax extensional{false, {}};
    for (unsigned l = 0; l < 2; ++l) {
        for (unsigned r = 0; r < 2; ++r) {
            expr cell = model.eval(related(enum_value(env.left_case, l),
                                               enum_value(env.right_case, r)), true);
            bool value = bool_literal(cell);
            if (value != extensional.default_value)
                extensional.writes.push_back({{l, r}, value});
        }
    }

    // The complete finite graph is canonical evidence extracted from the
    // model. lift decomposes this interned term; reify rebuilds it through the
    // same declarations and ast_manager, so hash-consing must return the same
    // live node rather than merely an equivalent array.
    expr canonical = reify(env, extensional);
    TableSyntax source = lift(env, canonical);
    expr roundtrip = reify(env, source);
    if (!Z3_is_eq_ast(ctx, canonical, roundtrip)) {
        std::cerr << "reify(lift(x)) violated exact AST identity\n";
        return EXIT_FAILURE;
    }

    std::cout << "sat: z3 filled model-shaped hole bisim\n";
    print_table(source);
    std::cout << "reify(lift(x)): exact ast identity (diagnostic ast_id: "
              << Z3_get_ast_id(ctx, canonical) << ")\n";
    return EXIT_SUCCESS;
}

} // namespace fine

int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "demo-bisim")
        return fine::demo_bisim();
    std::cerr << "usage: fine demo-bisim\n";
    return EXIT_FAILURE;
} catch (z3::exception const& error) {
    std::cerr << "z3: " << error.msg() << '\n';
    return EXIT_FAILURE;
} catch (std::exception const& error) {
    std::cerr << "fine: " << error.what() << '\n';
    return EXIT_FAILURE;
}
