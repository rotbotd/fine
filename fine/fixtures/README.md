# Two-level proof core fixtures

- `identity-coeffect.fine` forms an elaborator-only identity proof, absorbs it,
  resolves a function coeffect from exact caller-local evidence, and verifies a
  guarantee which needs the absorbed equality.
- `identity-coeffect-materialized.fine` is the exact explicit-`using` output;
  it reruns with implicit coeffect search forbidden.
- `reject-missing-coeffect.fine` has no caller proof for a demanded identity.
- `reject-proof-as-value.fine` tries to put proof evidence in an `Int` binding.
- `reject-unjustified-function.fine` removes the identity coeffect from the
  function whose guarantee needs it.
