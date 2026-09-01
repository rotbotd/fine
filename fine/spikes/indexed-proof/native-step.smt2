; The indices are literal native Z3 values.  There is no universal Term sort
; and no datatype of Step witnesses.  Spacer gives Step its least-relation
; meaning: only the base rule and its contextual closure are inhabited.
(set-logic HORN)

(declare-datatypes ((Tm 0))
  (((z)
    (s (pred Tm)))))

(declare-rel Step (Tm Tm))
(declare-rel Reachable ())
(declare-rel Junk ())
(declare-var before Tm)
(declare-var after Tm)

(rule (Step z (s z)))
(rule (=> (Step before after)
          (Step (s before) (s after))))

(rule (=> (Step (s (s z)) (s (s (s z)))) Reachable))
(rule (=> (Step z z) Junk))

; sat: two applications of the recursive constructor exist.
(query Reachable)
; unsat: leastness excludes an index pair made by no constructor.
(query Junk)
