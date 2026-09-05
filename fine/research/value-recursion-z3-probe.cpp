#include "api/c++/z3++.h"

#include <iostream>
#include <string_view>

using namespace z3;

int main(int argc, char** argv) {
  const bool run_unsafe_query =
      argc == 2 && std::string_view(argv[1]) == "--unsafe-nonterminating-query";

  context c;
  constructors constructors(c);
  symbol nat_name = c.str_symbol("Nat");
  symbol predecessor_name = c.str_symbol("predecessor");
  sort recursive_field[1] = {c.datatype_sort(nat_name)};
  constructors.add(c.str_symbol("zero"), c.str_symbol("is_zero"), 0, nullptr,
                   nullptr);
  constructors.add(c.str_symbol("successor"), c.str_symbol("is_successor"), 1,
                   &predecessor_name, recursive_field);
  sort nat = c.datatype(nat_name, constructors);

  func_decl zero(c), is_zero(c), successor(c), is_successor(c);
  func_decl_vector no_accessors(c), successor_accessors(c);
  constructors.query(0, zero, is_zero, no_accessors);
  constructors.query(1, successor, is_successor, successor_accessors);

  expr n = c.constant("n", nat);
  func_decl size = c.recfun("size", nat, c.int_sort());
  expr_vector size_arguments(c);
  size_arguments.push_back(n);
  c.recdef(size, size_arguments,
           ite(is_zero(n), c.int_val(0),
               1 + size(successor_accessors[0](n))));

  expr two = successor(successor(zero()));
  solver ground(c);
  ground.set("timeout", 1000u);
  ground.add(size(two) != 2);
  std::cout << "structural-ground: " << ground.check() << '\n';

  solver symbolic(c);
  symbolic.set("timeout", 1000u);
  symbolic.add(size(n) < 0);
  check_result symbolic_status = symbolic.check();
  std::cout << "structural-symbolic-nonnegative: " << symbolic_status;
  if (symbolic_status == unknown) {
    std::cout << " (" << symbolic.reason_unknown() << ')';
  }
  std::cout << '\n';

  func_decl even = c.recfun("even", nat, c.bool_sort());
  func_decl odd = c.recfun("odd", nat, c.bool_sort());
  expr_vector parity_arguments(c);
  parity_arguments.push_back(n);
  c.recdef(even, parity_arguments,
           ite(is_zero(n), c.bool_val(true),
               odd(successor_accessors[0](n))));
  c.recdef(odd, parity_arguments,
           ite(is_zero(n), c.bool_val(false),
               even(successor_accessors[0](n))));

  solver mutual(c);
  mutual.set("timeout", 1000u);
  mutual.add(!even(two));
  std::cout << "mutual-structural-ground: " << mutual.check() << '\n';

  expr x = c.int_const("x");
  func_decl loop = c.recfun("loop", c.int_sort(), c.int_sort());
  expr_vector loop_arguments(c);
  loop_arguments.push_back(x);
  c.recdef(loop, loop_arguments, loop(x) + 1);
  std::cout << "nonterminating-definition-added: true" << std::endl;

  if (!run_unsafe_query) {
    return 0;
  }

  solver nonterminating(c);
  nonterminating.set("timeout", 1000u);
  std::cout << "nonterminating-before-assert" << std::endl;
  expr loop_at_zero = loop(0);
  std::cout << "nonterminating-term-built" << std::endl;
  nonterminating.add(loop_at_zero == 0);
  std::cout << "nonterminating-assert-added" << std::endl;
  std::cout << "nonterminating-ground: " << nonterminating.check() << '\n';
}
