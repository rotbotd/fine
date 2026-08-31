(set-logic ALL)
(set-option :produce-models true)
(set-option :smt.mbqi true)

(declare-datatypes () ((LeftState left-0 left-1)))
(declare-datatypes () ((RightState right-0 right-1)))
(declare-datatypes ()
  ((Pair (pair (left-state LeftState) (right-state RightState)))))

; Both systems alternate forever between their two states.
(define-fun left-step ((from LeftState) (to LeftState)) Bool
  (= to (ite (= from left-0) left-1 left-0)))
(define-fun right-step ((from RightState) (to RightState)) Bool
  (= to (ite (= from right-0) right-1 right-0)))

; The Boolean observation distinguishes state 0 from state 1.
(define-fun left-label ((state LeftState)) Bool
  (= state left-1))
(define-fun right-label ((state RightState)) Bool
  (= state right-1))

; The model-shaped hole is one array value, indexed by the finite product.
(declare-const bisim (Array Pair Bool))

; Related states must have the same observation.
(assert
  (forall ((l LeftState) (r RightState))
    (=> (select bisim (pair l r))
        (= (left-label l) (right-label r)))))

; Every left transition must be matched on the right.
(assert
  (forall ((l LeftState) (r RightState) (l-next LeftState))
    (=> (and (select bisim (pair l r))
             (left-step l l-next))
        (exists ((r-next RightState))
          (and (right-step r r-next)
               (select bisim (pair l-next r-next)))))))

; Every right transition must be matched on the left.
(assert
  (forall ((l LeftState) (r RightState) (r-next RightState))
    (=> (and (select bisim (pair l r))
             (right-step r r-next))
        (exists ((l-next LeftState))
          (and (left-step l l-next)
               (select bisim (pair l-next r-next)))))))

; Ask for the bisimulation containing the two initial states.
(assert (select bisim (pair left-0 right-0)))

(check-sat)
(get-value (bisim))
(get-value ((select bisim (pair left-0 right-0))
            (select bisim (pair left-0 right-1))
            (select bisim (pair left-1 right-0))
            (select bisim (pair left-1 right-1))))
