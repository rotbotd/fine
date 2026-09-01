; Constructor introduction implications in the ordinary SMT solver are not an
; inductive family.  The unconstrained Bool function may contain arbitrary
; index pairs in addition to the constructor images.
(set-logic ALL)

(declare-datatypes ((Tm 0))
  (((z)
    (s (pred Tm)))))

(declare-fun Step (Tm Tm) Bool)
(assert (Step z (s z)))
(assert (forall ((before Tm) (after Tm))
  (=> (Step before after)
      (Step (s before) (s after)))))

; z -> z has no constructor, yet the intro-only encoding accepts it.
(assert (Step z z))
(check-sat)
