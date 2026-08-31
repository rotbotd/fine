#include "ast/array_decl_plugin.h"
#include "ast/datatype_decl_plugin.h"
#include "ast/reg_decl_plugins.h"
#include "util/memory_manager.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct memory_scope {
    memory_scope() { memory::initialize(0); }
    ~memory_scope() { memory::finalize(); }
};

enum class fine_kind {
    bool_false,
    bool_true,
    state_0,
    state_1,
    const_array,
    store,
};

// This is deliberately only the admitted constructor tree, not a second term
// representation. It is the small result type of lift and input type of reify.
struct fine_term {
    fine_kind kind;
    std::vector<fine_term> args;
};

class roundtrip {
    ast_manager& m;
    array_util arrays;
    sort* state_sort;
    sort* relation_sort;
    app* state_0;
    app* state_1;

    [[noreturn]] static void reject(char const* message) {
        throw std::runtime_error(message);
    }

public:
    roundtrip(ast_manager& manager, sort* states, app* s0, app* s1)
        : m(manager),
          arrays(manager),
          state_sort(states),
          relation_sort(arrays.mk_array_sort(states, manager.mk_bool_sort())),
          state_0(s0),
          state_1(s1) {}

    sort* array_sort() const { return relation_sort; }

    fine_term lift(expr* e) const {
        if (m.is_false(e))
            return {fine_kind::bool_false, {}};
        if (m.is_true(e))
            return {fine_kind::bool_true, {}};
        if (e == state_0)
            return {fine_kind::state_0, {}};
        if (e == state_1)
            return {fine_kind::state_1, {}};

        expr* value = nullptr;
        if (arrays.is_const(e, value)) {
            if (e->get_sort() != relation_sort)
                reject("const array has the wrong admitted array sort");
            return {fine_kind::const_array, {lift(value)}};
        }

        expr* base = nullptr;
        expr* key = nullptr;
        expr* value_at_key = nullptr;
        if (arrays.is_store1(e, base, key, value_at_key)) {
            if (e->get_sort() != relation_sort)
                reject("store has the wrong admitted array sort");
            return {fine_kind::store,
                    {lift(base), lift(key), lift(value_at_key)}};
        }

        reject("term is outside the admitted Fine array fragment");
    }

    expr_ref reify(fine_term const& t) {
        switch (t.kind) {
        case fine_kind::bool_false:
            if (!t.args.empty()) reject("false takes no arguments");
            return expr_ref(m.mk_false(), m);
        case fine_kind::bool_true:
            if (!t.args.empty()) reject("true takes no arguments");
            return expr_ref(m.mk_true(), m);
        case fine_kind::state_0:
            if (!t.args.empty()) reject("S0 takes no arguments");
            return expr_ref(state_0, m);
        case fine_kind::state_1:
            if (!t.args.empty()) reject("S1 takes no arguments");
            return expr_ref(state_1, m);
        case fine_kind::const_array: {
            if (t.args.size() != 1) reject("array takes one default argument");
            expr_ref value = reify(t.args[0]);
            if (!m.is_bool(value)) reject("array default is not Bool");
            return expr_ref(arrays.mk_const_array(relation_sort, value), m);
        }
        case fine_kind::store: {
            if (t.args.size() != 3) reject("store takes base, key, and value");
            expr_ref base = reify(t.args[0]);
            expr_ref key = reify(t.args[1]);
            expr_ref value = reify(t.args[2]);
            if (base->get_sort() != relation_sort)
                reject("store base is not Array(State, Bool)");
            if (key->get_sort() != state_sort)
                reject("store key is not State");
            if (!m.is_bool(value)) reject("store value is not Bool");
            expr* args[3] = {base, key, value};
            return expr_ref(arrays.mk_store(3, args), m);
        }
        }
        reject("unknown Fine constructor");
    }

    std::string print(fine_term const& t) const {
        switch (t.kind) {
        case fine_kind::bool_false: return "false";
        case fine_kind::bool_true: return "true";
        case fine_kind::state_0: return "S0";
        case fine_kind::state_1: return "S1";
        case fine_kind::const_array:
            return "array(default: " + print(t.args.at(0)) + ")";
        case fine_kind::store:
            return "store(" + print(t.args.at(0)) + ", key: " +
                   print(t.args.at(1)) + ", value: " +
                   print(t.args.at(2)) + ")";
        }
        reject("unknown Fine constructor");
    }
};

struct state_datatype {
    sort* state_sort;
    app_ref s0;
    app_ref s1;
};

state_datatype mk_state_datatype(ast_manager& m) {
    datatype_util util(m);
    auto* plugin = static_cast<datatype_decl_plugin*>(
        m.get_plugin(m.get_family_id("datatype")));
    if (!plugin)
        throw std::runtime_error("datatype plugin was not registered");

    constructor_decl* s0_decl =
        mk_constructor_decl(symbol("S0"), symbol("is-S0"), 0, nullptr);
    constructor_decl* s1_decl =
        mk_constructor_decl(symbol("S1"), symbol("is-S1"), 0, nullptr);
    constructor_decl* constructors[2] = {s0_decl, s1_decl};
    datatype_decl* declaration = mk_datatype_decl(
        util, symbol("State"), 0, nullptr, 2, constructors);

    sort_ref_vector sorts(m);
    if (!plugin->mk_datatypes(1, &declaration, 0, nullptr, sorts))
        throw std::runtime_error("Z3 rejected the State datatype");

    sort* state = sorts[0].get();
    ptr_vector<func_decl> const& ctors = *util.get_datatype_constructors(state);
    if (ctors.size() != 2)
        throw std::runtime_error("State did not receive exactly two constructors");

    return {state,
            app_ref(m.mk_const(ctors[0]), m),
            app_ref(m.mk_const(ctors[1]), m)};
}

} // namespace

int main() {
    // Internal AST clients must perform the initialization normally hidden by
    // the public API/context wrapper. In particular this initializes symbols.
    memory_scope memory;
    ast_manager manager;
    // Datatype finalization asks the sequence and character plugins about
    // recursive fields, even for this two-case enum. Preserve Z3's complete
    // built-in registration order rather than assembling a fragile subset.
    reg_decl_plugins(manager);

    state_datatype states = mk_state_datatype(manager);
    roundtrip fine(manager, states.state_sort, states.s0, states.s1);
    array_util arrays(manager);

    // Canonical table: S0 -> false, S1 -> true. The false cell is the
    // deterministic default, so only the differing S1 cell is stored.
    expr_ref default_false(
        arrays.mk_const_array(fine.array_sort(), manager.mk_false()), manager);
    expr* store_args[3] = {default_false, states.s1, manager.mk_true()};
    expr_ref witness(arrays.mk_store(3, store_args), manager);

    fine_term lifted = fine.lift(witness);
    expr_ref reified = fine.reify(lifted);
    bool exact_identity = witness.get() == reified.get();

    std::cout << "fine: " << fine.print(lifted) << '\n';
    std::cout << "original pointer: " << static_cast<void*>(witness.get())
              << " ast_id=" << witness->get_id() << '\n';
    std::cout << "reified pointer:  " << static_cast<void*>(reified.get())
              << " ast_id=" << reified->get_id() << '\n';
    std::cout << "reify(lift(witness)) pointer-identical: "
              << (exact_identity ? "yes" : "NO") << '\n';

    return exact_identity ? 0 : 1;
}
