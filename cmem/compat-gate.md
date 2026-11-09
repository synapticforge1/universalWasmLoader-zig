# The compatibility gate (workspace letter H6, built 2026-10-26)

🔒 Binding (owner, 2026-09-20; workspace `../../../cmem/layout-and-packaging.md` § "The
compatibility gate"): "backward compatible" is a gate result, never a judgment made at bump time.
The floor and previous-tag logic follow loader-c's `scripts/compat.ts`
(`../universalWasmLoader-c/cmem/compat-gate.md`); the surface generator is this repo's own.

## What runs where

| Piece | File | Runs in |
| --- | --- | --- |
| Rule 1, the SURFACE | `tools/surface.zig` (a `zig build surface` step) + `scripts/surface.ts` → `api-surface.txt` (committed) | `bun scripts/surface.ts` writes it; `--check`, `bump-version.ts` and `compat.ts` refuse a stale one |
| Rule 2, the FLOOR | `scripts/compat.ts` `floorOf` | `bump-version.ts` (refuses below it, `--dry-run` included), `compat.ts` (report), `compat.ts --release` inside `release.ts` |
| Rule 3, BEHAVIOUR | `scripts/compat.ts` `previousTests` | the same places; required green for a patch or minor |
| Rule 4, deprecate first | README `### ⚠️ Breaking Changes` table | by hand, when a deprecation lands |
| Rule 5, re-baseline alone | `surface.ts --check` | an intended surface change is its own commit (`bun scripts/surface.ts`), with the reason |

**The surface** is read by REFLECTION over the compiled `uwl` module (`@typeInfo`), not by parsing
source text, so it is what a Zig consumer actually sees: every pub decl of `uwl` (error-set members
sorted, struct fields, functions as parameter types + calling convention + return type), and the
raw C layer `uwl.c` — every `uwl_` / `UWL_` name the translated header exposes, constants with their integer
VALUE (C integers are frozen), struct layouts including the anonymous `uwl_val.as` union. The C
calling convention is keyed as `c`, so the surface is the same on every target. `surface.ts` adds
the SPEC version from the header's leading comment (exactly once, as -c reads it). A form the tool
cannot key is a compile error, never skipped. **112 symbols, SPEC 3.0.0, on 2026-10-26; 118 symbols,
SPEC 4.0.0, on 2026-11-08** (`importWith`, `lastError`, `HostCallback` + 3 fields; `8eae638`).
Floor vs `v1.0.0` on 2026-11-08: **major** (`bump patch` refused, `bump major --dry-run` allowed).
⚠️ A generic function's return type is invisible to reflection (`Module.call` prints `generic`);
Rule 3 is what sees a change there.

**The floor:** removed or changed → major; added → minor; nothing → patch; the SPEC version moves it
by its own level (lowered → major). A changed signature is major even when it only widens (errs
high, as -c and -js).

**A previous tag's surface** (since 2026-11-08) is that tag's committed `api-surface.txt` when
it has one — the release gate refuses a stale snapshot, so at a tag the snapshot IS the surface,
whatever Zig built it. A tag without one is built from its `src/` (`git archive` into `.compat/`,
gitignored, deleted after) with TODAY's `tools/surface.zig`, through `build.zig`'s `-Dsrc=`; if
that `src/` does not build on today's Zig, `floorVs` returns floor **major** with the reason
(erring high). That is `v1.0.0` since the Zig 0.17.1 port: its `src/uwl.zig` uses `@cImport`,
removed in 0.17. So the first release after `v1.0.0` must be a major — which SPEC 4.0.0 (ledger
C29) makes it anyway.
**Its tests** run the same way through `-Dtest-root=` / `-Dtest-cwd=` (fixtures travel with the
suite; `wasmtime.dll` is found through PATH, which `build.zig`'s `prependPath` sets). The verdict
is read from the OUTPUT (`All N tests passed.` / `N passed; M skipped; K failed.`) and the exit
code.

**Toolchain-stable keys (2026-11-08):** the translated header's namespace is the BUILD's name for
the header module (`cimport.` under Zig 0.16's `@cImport`, `universal_wasm_loader.` under 0.17's
`addTranslateC`), not API; `tools/surface.zig`'s `typeName` keys it as `c.`. The 0.17.1 port's
re-baseline changed exactly those 66 lines and nothing else (measured: with `cimport.` → `c.`
applied to the old snapshot, every line equal).

**The version line** is `build.zig.zon` `.version`; `bump-version.ts` refuses unless it equals the
last release tag. **`v1.0.0` has no tests**, so the first bump after it prints `NOTHING TO RUN (Rule
3 unverified for this step)` and proceeds — Rule 3 starts biting at the first tag that carries
`tests/uwl_test.zig`. Measured 2026-10-26: floor vs `v1.0.0` = patch (`src/` unchanged since it).

**Consumers (Rule 3, second half):** wasmtk's H6.5b consumer suites name this repo's suite; none
run here.

## Inverted end to end, 2026-10-26 (a scratch clone; nothing in the repo was touched)

| Mutant | Command | Result |
| --- | --- | --- |
| clean tree | `compat`, `bump patch --dry-run` | floor patch; allowed (Rule 3: nothing to run at v1.0.0) |
| `pub fn ping` added to `Module`, NOT re-baselined | `surface --check`, `bump patch` | both refuse: stale, `added: fn Module.ping` |
| same, re-baselined | `bump patch` / `minor` | refused (floor minor) / allowed → 1.1.0 |
| `Module.import` parameter `[:0]const u8` → `[*:0]const u8`, re-baselined | `bump minor` / `major` | refused (floor major, `changed: fn Module.import`) / allowed → 2.0.0 |
| SPEC lowered 3.0.0 → 2.9.0, re-baselined | `bump minor` | refused (floor major) |
| scratch tag v1.0.1 WITH tests; bool decoding inverted (surface unchanged) | `surface --check`; `compat`; `bump patch` / `major` | green (correct: no API change); previous-tag tests RED 10 / 1; refused / allowed |
| `.version` 1.0.2 with an addition over v1.0.1 | `compat --release` | BLOCKED: patch below the floor (minor); 1.1.0 passes |

## Traps met building it

- Translate-c turns functions taking a struct into METHODS of that struct as well
  (`c.uwl_val.i32`, `c.uwl_val_t.uwl_as_bool`); they are reachable, so they are surface.
- An error set's member order and the C calling convention's tag (`x86_64_win`) are not stable
  across declarations / targets; both are normalized in `tools/surface.zig`.
