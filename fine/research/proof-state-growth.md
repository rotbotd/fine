# Proof-state growth across live budgets

This note answers one narrow question: should the live identity selector version
part of its immutable Z3 datatype graph when a larger proof budget discovers new
productions, rather than resetting the whole state family?

The reproducible input is `identity-checkpoint.fine` at budgets one through four.
For each budget, `fine rain --checkpoint` retains the direct grammar's structured
productions and complete state graph. `profile_proof_state_growth.py` compares
those graphs by stable production content rather than the numeric production
indices, then computes three sets.

1. **Directly extended states.** An old score/type state gained a transition.
2. **Affected parents.** An otherwise unchanged old state points transitively to
   a directly extended child. Its datatype must also be versioned because an old
   constructor field cannot be retargeted to a new child sort.
3. **Safely reusable states.** The remaining shared states have identical
   alternatives and no path to a versioned child.

The checked result is in `proof-state-growth-profile.json`:

| growth | new productions | old states | directly extended | affected closure | safely reusable | new states |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 → 2 | 12 | 6 | 0 | 0 | 6 | 22 |
| 2 → 3 | 14 | 28 | 6 | 6 | 22 | 92 |
| 3 → 4 | 0 | 120 | 0 | 0 | 120 | 79 |

The current reset loses six tiny states at the first growth and 22 at the second.
The first expensive family is budget three's 120 states, and the existing stable-
production path already retains all 120 while adding the 79 budget-four states.
A native end-to-end timing probe ran 100 complete four-epoch checkpoint searches
in 2.781 seconds on this machine; this measures parser, grammar discovery, Z3,
lifting, Rainfall, and file writes together, not datatype rebuilding alone and
not browser performance.

Selective versioning is therefore rejected for now. It would require stable
production identities across reordered canonical vectors, multiple generations
of the same score/type state, retargeting every affected parent, and model lifting
across mixed generations. That machinery would protect 28 small early states in
this discriminator while the 120-state expensive boundary is already reused.
Reopen the decision only with a fixture where productions continue growing after
the state family is large, or a browser profile isolating datatype reconstruction
as a material fraction of an epoch.

Reproduce the report from a development build:

```sh
directory=$(mktemp -d)
for budget in 1 2 3 4; do
  .build/fine rain --checkpoint --proof-budget "$budget" \
    fine/fixtures/identity-checkpoint.fine > "$directory/$budget.rain"
done
python3 fine/profile_proof_state_growth.py \
  "$directory/1.rain" "$directory/2.rain" \
  "$directory/3.rain" "$directory/4.rain"
```
