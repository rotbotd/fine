#include "c++/z3++.h"

#include <iostream>
#include <set>
#include <string>

using namespace z3;

static void collect_proof_operators(expr const &value, std::set<std::string> &names, std::set<unsigned> &visited) {
    if (!visited.insert(value.id()).second || !value.is_app())
        return;
    // The public sort-kind enum has no dedicated proof case; proof terms expose
    // the internal sort name `Proof` through the ordinary AST interface.
    if (value.get_sort().name().str() == "Proof")
        names.insert(value.decl().name().str());
    for (unsigned index = 0; index < value.num_args(); ++index)
        collect_proof_operators(value.arg(index), names, visited);
}

static void print_proof_operators(char const *label, expr const &proof) {
    std::set<std::string> names;
    std::set<unsigned> visited;
    collect_proof_operators(proof, names, visited);
    std::cout << label << " proof operators:";
    for (auto const &name : names)
        std::cout << " " << name;
    std::cout << "\n";
}

static void native_boolean_proof() {
    config cfg;
    cfg.set("proof", true);
    context c(cfg);
    solver s(c);
    expr a = c.bool_const("a");
    s.add(a);
    s.add(!a);

    std::cout << "native-boolean status: " << s.check() << "\n";
    expr proof = s.proof();
    std::cout << "native-boolean proof: " << proof << "\n";
    print_proof_operators("native-boolean", proof);
}

static void native_symmetry_proof(bool integer_theory) {
    config cfg;
    cfg.set("proof", true);
    context c(cfg);
    solver s(c);

    sort carrier = integer_theory ? c.int_sort() : c.uninterpreted_sort("Carrier");
    expr left = c.constant("left", carrier);
    expr right = c.constant("right", carrier);
    s.add(left == right);
    s.add(!(right == left));

    char const *label = integer_theory ? "native-int-symmetry" : "native-uf-symmetry";
    std::cout << label << " status: " << s.check() << "\n";
    expr proof = s.proof();
    if (!integer_theory)
        std::cout << label << " proof: " << proof << "\n";
    print_proof_operators(label, proof);
}

static char const *symmetry_hole_script = R"SMT(
(declare-datatypes ((SrcProof 0))
  (((local-p)
    (refl (refl-value Int))
    (apply-symm (symm-argument SrcProof)))))
(define-funs-rec
 ((src ((p SrcProof)) Int)
  (dst ((p SrcProof)) Int)
  (cost ((p SrcProof)) Int))
 ((ite ((_ is local-p) p) 1
    (ite ((_ is refl) p) (refl-value p)
      (dst (symm-argument p))))
  (ite ((_ is local-p) p) 2
    (ite ((_ is refl) p) (refl-value p)
      (src (symm-argument p))))
  (ite ((_ is apply-symm) p) (+ 1 (cost (symm-argument p))) 1)))
(declare-const hole SrcProof)
(assert (= (src hole) 2))
(assert (= (dst hole) 1))
(assert (<= (cost hole) 2))
)SMT";

static char const *composition_hole_script = R"SMT(
(declare-datatypes ((SrcProof 0))
  (((local-12)
    (local-23)
    (refl (refl-value Int))
    (apply-symm (symm-argument SrcProof))
    (apply-trans (trans-left SrcProof) (trans-right SrcProof)))))
(define-funs-rec
 ((src ((p SrcProof)) Int)
  (dst ((p SrcProof)) Int)
  (well ((p SrcProof)) Bool)
  (cost ((p SrcProof)) Int))
 ((ite ((_ is local-12) p) 1
    (ite ((_ is local-23) p) 2
      (ite ((_ is refl) p) (refl-value p)
        (ite ((_ is apply-symm) p) (dst (symm-argument p))
          (src (trans-left p))))))
  (ite ((_ is local-12) p) 2
    (ite ((_ is local-23) p) 3
      (ite ((_ is refl) p) (refl-value p)
        (ite ((_ is apply-symm) p) (src (symm-argument p))
          (dst (trans-right p))))))
  (ite ((_ is apply-trans) p)
    (and (well (trans-left p))
         (well (trans-right p))
         (= (dst (trans-left p)) (src (trans-right p))))
    (ite ((_ is apply-symm) p) (well (symm-argument p)) true))
  (ite ((_ is apply-trans) p)
    (+ 1 (cost (trans-left p)) (cost (trans-right p)))
    (ite ((_ is apply-symm) p) (+ 1 (cost (symm-argument p))) 1))))
(declare-const hole SrcProof)
(assert (well hole))
(assert (= (src hole) 3))
(assert (= (dst hole) 1))
(assert (<= (cost hole) 4))
)SMT";

static char const *universal_symmetry_script = R"SMT(
(declare-datatypes ((SrcProof 0))
  (((local-12)
    (local-23)
    (refl (refl-value Int))
    (apply-symm (symm-argument SrcProof))
    (apply-trans (trans-left SrcProof) (trans-right SrcProof)))))
(define-funs-rec
 ((src ((p SrcProof)) Int)
  (dst ((p SrcProof)) Int)
  (well ((p SrcProof)) Bool))
 ((ite ((_ is local-12) p) 1
    (ite ((_ is local-23) p) 2
      (ite ((_ is refl) p) (refl-value p)
        (ite ((_ is apply-symm) p) (dst (symm-argument p))
          (src (trans-left p))))))
  (ite ((_ is local-12) p) 2
    (ite ((_ is local-23) p) 3
      (ite ((_ is refl) p) (refl-value p)
        (ite ((_ is apply-symm) p) (src (symm-argument p))
          (dst (trans-right p))))))
  (ite ((_ is apply-trans) p)
    (and (well (trans-left p))
         (well (trans-right p))
         (= (dst (trans-left p)) (src (trans-right p))))
    (ite ((_ is apply-symm) p) (well (symm-argument p)) true))))
(declare-fun synth-symm (SrcProof) SrcProof)
(assert (forall ((input SrcProof))
  (=> (well input)
      (and (well (synth-symm input))
           (= (src (synth-symm input)) (dst input))
           (= (dst (synth-symm input)) (src input))))))
)SMT";

static void solve_model(char const *label, char const *script, unsigned timeout_ms) {
    context c;
    solver s(c);
    params options(c);
    options.set("timeout", timeout_ms);
    s.set(options);
    s.from_string(script);
    check_result result = s.check();

    std::cout << label << " status: " << result << "\n";
    if (result == sat)
        std::cout << label << " model:\n" << s.get_model() << "\n";
    else if (result == unknown)
        std::cout << label << " reason: " << s.reason_unknown() << "\n";
}

static void solve_model_with_tactic(char const *label, char const *script, char const *tactic_name,
                                    unsigned timeout_ms) {
    context c;
    tactic engine(c, tactic_name);
    solver s = engine.mk_solver();
    params options(c);
    options.set("timeout", timeout_ms);
    s.set(options);
    s.from_string(script);
    check_result result = s.check();

    std::cout << label << " status: " << result << "\n";
    if (result == sat)
        std::cout << label << " model:\n" << s.get_model() << "\n";
    else if (result == unknown)
        std::cout << label << " reason: " << s.reason_unknown() << "\n";
}

int main() {
    native_boolean_proof();
    native_symmetry_proof(false);
    native_symmetry_proof(true);
    solve_model("ground-symmetry-hole", symmetry_hole_script, 5000);
    solve_model("ground-composition-hole", composition_hole_script, 5000);
    solve_model("universal-symmetry-scheme", universal_symmetry_script, 5000);
    solve_model_with_tactic("qsat-universal-symmetry-scheme", universal_symmetry_script, "qsat", 5000);
}
