; A variable present only in a Horn body is an existential search variable for
; deriving the head.  It therefore cannot encode "for every fresh name".
(set-logic HORN)

(declare-datatypes ((Name 0))
  (((a) (b))))

(declare-rel Fresh (Name))
(declare-rel BodyStep (Name))
(declare-rel BodyStepFails (Name))
(declare-rel UnderBinder ())
(declare-rel CofiniteCounterexample ())
(declare-var name Name)

(rule (Fresh a))
(rule (Fresh b))
(rule (BodyStep a))
(rule (BodyStepFails b))

; This tempting lowering derives UnderBinder from the one working fresh name.
(rule (=> (and (Fresh name) (BodyStep name)) UnderBinder))
; At the same time b explicitly witnesses failure of the intended universal.
(rule (=> (and (Fresh name) (BodyStepFails name)) CofiniteCounterexample))

(query UnderBinder)
(query CofiniteCounterexample)
