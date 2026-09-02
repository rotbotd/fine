#!/usr/bin/env bash
set -euo pipefail

here="$(cd -- "$(dirname -- "$0")" && pwd)"
root="$(git -C "$here" rev-parse --show-toplevel)"
build="$root/build/proof-core"

if [[ ! -f "$build/libz3.a" ]]; then
  cmake -S "$root" -B "$build" -G Ninja \
    -DZ3_BUILD_LIBZ3_SHARED=OFF \
    -DZ3_BUILD_EXECUTABLE=OFF \
    -DZ3_BUILD_TEST_EXECUTABLES=OFF \
    -DFINE_BUILD_EXECUTABLE=ON
fi
cmake --build "$build" --target libz3 -j"$(nproc)"

c++ -std=c++17 -O2 \
  -I"$root/src/api" \
  -I"$build/src" \
  "$here/probe.cpp" "$build/libz3.a" -pthread \
  -o "$here/probe"

output="$("$here/probe")"
printf '%s\n' "$output"

grep -Fq "native-boolean status: unsat" <<<"$output"
grep -Fq "native-uf-symmetry proof operators: asserted mp rewrite symm unit-resolution" \
  <<<"$output"
grep -Fq "native-int-symmetry proof operators: asserted monotonicity mp not-or-elim rewrite trans unit-resolution" \
  <<<"$output"
grep -Fq "ground-symmetry-hole status: sat" <<<"$output"
grep -Fq "  (apply-symm local-p))" <<<"$output"
grep -Fq "ground-composition-hole status: sat" <<<"$output"
grep -Fq "  (apply-symm (apply-trans local-12 local-23)))" <<<"$output"
grep -Fq "universal-symmetry-scheme status: unknown" <<<"$output"
grep -Fq "universal-symmetry-scheme reason: timeout" <<<"$output"
grep -Fq "qsat-universal-symmetry-scheme status: unknown" <<<"$output"
grep -Fq "qsat-universal-symmetry-scheme reason: formula contains uninterpreted functions" \
  <<<"$output"
