# Direct bounded proof grammar spike

This isolates the timeout recorded in `LOG.md` for the first all-frontier
checkpoint selector. The existing selector uses one recursive datatype plus
recursive scoring and type functions. The alternative unfolds the bounded
grammar into states indexed by exact proof type, constructor cost, completeness,
closed frontier, and open-leaf count. Every constructor field points to a
strictly cheaper state sort, so Z3 sees an acyclic family of finite datatypes
rather than recursive functions over an unbounded datatype.

The spike deliberately enumerates feasible state transitions, not concrete
proof trees. It uses the production union for the three-endpoint identity
checkpoint discriminator, asks both encodings for a model, lifts the direct
model by constructor identity, and reports construction and solve time. A small
concrete-tree oracle independently enumerates the same bounded fixture: the
spike requires every exact state to agree and rejects a lifted model absent from
the oracle's matching state.

Build and run against the repository's already-built Z3:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -Isrc/fine -Isrc/api -Isrc -I.build/src \
  spikes/direct-proof-grammar/main.cpp src/fine/proof_model_selector.cpp \
  .build/libz3.a -pthread -o /tmp/direct-proof-grammar
/tmp/direct-proof-grammar
```

This is not production code. In particular, production discovery still needs
to be detached from concrete candidate-tree enumeration, and equal-ranked root
alternatives need a stated deterministic tie-break before this can replace the
reference frontier.
