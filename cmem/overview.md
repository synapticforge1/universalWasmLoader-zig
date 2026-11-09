# overview — universalWasmLoader-zig

## What this is

`universalWasmLoader-zig` is the **idiomatic Zig binding** of the Universal WASM Loader family. It is
a thin, zero-duplication wrapper over the C single-header loader
([`universalWasmLoader-c`](https://github.com/synapticforge1/universalWasmLoader-c)) — the Zig sibling of
the reference JS loader [`@jrmarcum/universal-wasm-loader`](https://jsr.io/@jrmarcum/universal-wasm-loader).
The hard parts (WIT parsing, Canonical ABI marshalling, `cabi_post` string returns, version pinning)
live **once** in the C loader; this repo only adds a Zig-native call surface on top.

- **Language / runtime:** Zig **0.17.1** (`minimum_zig_version = "0.17.1"`, since 2026-11-08;
  0.16.0 before), built on the [wasmtime C API](https://docs.wasmtime.dev/c-api/) (v45.0.2) as
  the WASM engine. See "Zig 0.17.1 port" below for what moved.
- **What it gives Zig callers:** error unions instead of out-params, native Zig argument/result types,
  and a **comptime-typed call site** (`m.call(i32, "add", .{3,4})`) — no manual `uwl_val_t` boxing.
- **Distribution:** a Zig module (`build.zig.zon` name `universal_wasm_loader`, v2.0.0 since
  2026-11-08 (v1.0.0 before), MIT) exposing
  the importable module `uwl` (`b.addModule("uwl", ...)`). The MIT `LICENSE` file is listed in the
  manifest `paths`, so it ships with the published package.

## Repository / hosting (moved 2026-10-21)

- **Home:** [`synapticforge1/universalWasmLoader-zig`](https://github.com/synapticforge1/universalWasmLoader-zig)
  — a GitHub organization owned by the owner. Transferred from the personal account `jrmarcum`;
  GitHub redirects the old `jrmarcum/...` URLs (web + git). The upstream C loader moved with it:
  [`synapticforge1/universalWasmLoader-c`](https://github.com/synapticforge1/universalWasmLoader-c).
- **Not moved:** the JSR package is still `@jrmarcum/universal-wasm-loader` (JSR scopes are separate
  from GitHub; `@synapticforge1/...` does not exist as of 2026-10-21). Keep JSR links as `@jrmarcum`.
- **Local clone:** `origin` was repointed to
  `https://github.com/synapticforge1/universalWasmLoader-zig.git` on 2026-10-21 (owner's machine).
  Other clones still on the old `jrmarcum/...` URL keep working via redirect; repoint them with
  `git remote set-url origin <new url>`.
- **Release scripts are owner-agnostic** — `release`/`publish` use `origin` and `gh` against the
  current repo, so nothing in them hard-codes the owner. The Zigistry `zig-package` topic/listing
  follows the repo to its new owner path.

## Current state — v2.0.0 (2026-11-08: SPEC 4.0.0, Zig 0.17.1; v1 implemented 2026-06-19)

Working tree:

- `src/uwl.zig` — the binding (public API + comptime arg/result dispatch). Imports the C header as
  the module `c` (translated by `build.zig`'s `addTranslateC`) and re-exports it as `uwl.c` (raw
  escape hatch).
- `src/uwl_impl.c` — 3-line TU that `#define UWL_IMPLEMENTATION` + includes the header to compile the
  loader body into the module.
- `src/universal_wasm_loader.h` — **vendored copy** of the upstream C single-header loader. Keep in
  sync with the `universalWasmLoader-c` release being targeted. **Provenance (★ 3.8), 2026-11-08:**
  copied with `git show v2.0.0:universal_wasm_loader.h` from loader-c (tag commit `56217b6`, the
  tag CI green per its record); sha256
  `5ffa482da1b788df6b5f4fbeac61ed899bac88ac2a67f6a08f659e2a80ee9e80`, = loader-c's working tree
  the same day; **SPEC 4.0.0**. Before: `f3af6143…` = loader-c v1.1.1 (= v1.1.0), SPEC 3.0.0,
  measured 2026-10-26; first copied 2026-06-19 from an unrecorded tag. On the next sync,
  re-stamp: the -c tag, the sha256, the date.
- `examples/adder.zig` — the runnable example (`zig build run`); loads `tests/math_50.wasm`. Its
  own `examples/math_50.*` copy (an older build, wasm bytes NOT the stamped set) was deleted
  2026-10-26 so one copy exists (★ 4.1).
- `tests/` (2026-10-26, workspace letter H7) — `uwl_test.zig` (the SPEC §8 suite) and the
  workspace's **stamped reference fixtures**, copied UNMODIFIED from universalWasmLoader-js
  `36aa661` (= loader-c `39050b1`, byte-identical): the five `*_50.wasm`/`.wit`,
  `init_check.wat`/`.wasm`, and `fixtures.json` (generator wasmtk `4a5c3cf668d`,
  `v2.0.3-64-g4a5c3cf`). Never regenerate them here — copy a new stamped set from -js (loader-dart
  found `generator.describe` differs per clone). `.gitattributes` pins LF and marks `*.wasm`
  binary, so the bytes the hashes cover are never converted.
- `build.zig` — compiles the C loader, links wasmtime, stages `wasmtime.dll`, defines the `run`,
  `test` and `surface` steps and the gate's `-Dsrc` / `-Dtest-root` / `-Dtest-cwd` options.
- `scripts/*.ts` — **all tooling, Bun + TypeScript only** ([INDEX.md](INDEX.md) § "Scripting rule"),
  run as `bun scripts/<name>.ts`; no `package.json`, no dependencies (Bun globals + `node:` built-ins).
  - `fetch-wasmtime.ts` — downloads the wasmtime C API SDK into `vendor/` (gitignored).
  - `bump-version.ts` / `release.ts` / `publish.ts` — the release toolchain (see "Release / publish
    flow" below).
  - `surface.ts` / `compat.ts` — the compatibility gate (2026-10-26, [compat-gate.md](compat-gate.md));
    `tools/surface.zig` is its `zig build surface` step (a build step, not a script — the owner's
    wazmrt ruling), `api-surface.txt` its committed snapshot.
  - `_lib.ts` — shared helpers (repo root, `exec`/`capture`/dry-run `runner`, `.version` read, git
    checks); not runnable on its own.
- **Migration record (2026-10-21):** the four Bash scripts and three Nushell twins were ported 1:1
  (same args, flags, guards, messages, exit codes 1/2) and deleted. Verified in a scratch clone on
  `C:`: `bump-version patch` committed a correct `.version` bump; `release --dry-run --no-build`
  printed tag/push/`gh release` plan and refused a tag not at HEAD; `publish --dry-run` downgraded
  guards to warnings; `fetch-wasmtime.ts` downloaded + extracted v45.0.2 and `zig build run` against
  it printed the expected adder output. The repo never had Deno scripts or config.
- `vendor/` — wasmtime C API SDK (headers + `libwasmtime.dll.a` + `wasmtime.dll`). **gitignored**;
  fetched on demand.
- `LICENSE` — MIT (Copyright (c) 2026 Jon Marcum), identical to the `-c` port. Listed in
  `build.zig.zon` `paths`; README License section links it.
- `CLAUDE.md` + `cmem/` — the auto-loaded pointer banner and this portable project memory.

Build environment used for v1: Zig 0.16.0 + wasmtime C API **v45.0.2** `x86_64-mingw`.

### Zig 0.17.1 port (2026-11-08, owner: "update this project to version 0.17.1 of zig")

Measured on the host's `zig` 0.17.1 (scoop, on PATH since 2026-11-05). Five breaks, each a
removed API, each fixed at its root (no shim):

| 0.16 form | 0.17.1 form | Where |
| --- | --- | --- |
| `@cImport({ @cInclude(...) })` | `b.addTranslateC(.{ .root_source_file = header })` → module import `"c"` | `build.zig`, `src/uwl.zig` |
| `b.args` + `run.addArgs` | `run.addPassthruArgs()` | `build.zig` (`run` step) |
| `Step.Run.addPathDir(dir)` | env map: `run.getEnvMap()` + `setEnvironmentVariable("PATH", …)` (`prependPath`) | `build.zig` (`test`, `surface`) |
| `b.pathFromRoot(p)` | `b.root.joinString(alloc, p)` | `build.zig` (`prependPath`) |
| `@typeInfo` `.fields` / `.decls` / `.params` (records) | `field_names` + `field_types`, `decl_names`, `param_types`, `attrs.varargs` / `attrs.@"callconv"`, error set `.error_names`, enum `field_values` | `src/uwl.zig` (tuple args), `tools/surface.zig` |

Two gate consequences, fixed in `tools/surface.zig` / `scripts/compat.ts` (see
[compat-gate.md](compat-gate.md)): the translated header's namespace (`cimport.` under 0.16,
`universal_wasm_loader.` under 0.17) leaked into 66 surface lines, now keyed as `c.`; and
`v1.0.0`'s `src/` no longer builds (it uses `@cImport`), so its surface is not computable.
Result: `zig build test` 11/11, `zig build run` the three adder lines, unchanged from 0.16.0.

## Public API surface (SPEC 4.0.0, 2026-11-08; workspace ledger C29)

All in `src/uwl.zig`. Idiomatic Zig over the C loader's explicit `uwl_call` core (NOT the C variadic
convenience layer). The C loader 2.0.0 does the checking (the `.wit` read first and parsed
strictly, every import's callback, each argument's kind, each callback's return kind); the binding
sends exact kinds, checks the result kind, and keeps the message. 118 symbols in `api-surface.txt`.

- **`uwl.Module`** — a loaded, ABI-translated module (`handle: *c.uwl_module_t`).
- **`Module.import(path: [:0]const u8) Error!Module`** = `importWith(path, &.{})`: a module that
  imports nothing (one with WIT imports is refused at load, naming every missing callback).
- **`Module.importWith(path, callbacks: []const HostCallback) Error!Module`** (new 2026-11-08) —
  wraps `uwl_import`. **`uwl.HostCallback`** = `c.uwl_host_callback_t` (name, `callconv(.c)` fn,
  userdata). Before 2026-11-08 host imports needed the raw `uwl.c` layer (and SPEC § 7.1's
  `hostCallbacks?` parameter was missing from the Zig API).
- **`m.call(comptime T, name, args) Error!T`** — `i32`/`i64`/`f32`/`f64`/`bool`/`void`. A result
  whose kind is not `T`'s (`void` ↔ UWL_VOID included) is `error.WrongResultType` with a message
  `export 'add' returned s32, not f64`. Before 2026-11-08 it read the wrong union member silently
  (divergence S8; the M2 mutant re-measured it: `call(f64, "add", 3, 4)` → `-8.9e66`).
  Unsupported `T` is a `@compileError`. Stricter than -c's `uwl_call_void`, which discards a result.
- **`m.callStr(allocator, name, args) Error![]u8`** — result **copied into `allocator`**, owned by
  the caller; any non-string result is `error.WrongResultType`.
- **`m.deinit()`** — `uwl_free` + poisons the handle.
- **`uwl.lastError() []const u8`** (new 2026-11-08) — a thread-local copy (1 KiB, truncated) of the
  C error string from this thread's last failed `import*` / `call` / `callStr`; `""` after a
  success. Before, the C string was freed unread, so a Zig caller could not learn WHY anything
  failed — and SPEC 4.0.0's refusals are defined by what their message names.
- **`uwl.Error`** = `{ ImportFailed, CallFailed, WrongResultType, OutOfMemory }`.
- **`uwl.c`** — the raw translated-header namespace; the singleton and the pool are there only.

### Argument marshalling (comptime, by Zig type — never a conversion)

`toVal(v)`: `i32` → `uwl_i32` (s32), `i64` → `uwl_i64` (s64), `f32` → `uwl_f32`, `f64` →
`uwl_f64`, `bool` → `uwl_bool`, a type coercing to `[]const u8` (slice or string literal) →
`uwl_strn`; an integer literal is s32 if it fits, else s64 (outside s64: compile error); a float
literal is f64. Any other type (`u8`, `usize`, `f16`, `[*:0]const u8`, …) is a compile error naming
the accepted types. Chosen 2026-11-08 because the C API has no export-signature introspection, so
the Zig side cannot see the WIT type; the type IS the kind, as SPEC § 4's port column says ("the
language's 32-bit integer", "the floating type"), and the loader refuses a mismatch naming the
export and the parameter. Until then every integer went as s64 and every float as f64 and the
loader narrowed — SPEC 4.0.0 calls that coercion. `uwl_strn` COPIES; `raw()` frees every argument
after the call (until 2026-11-08 the copies leaked, one per string argument per call).

## How it links

- `build.zig` adds the module `uwl` with `link_libc = true`, include paths for `src/` (vendored
  header) and the wasmtime SDK `include/`, and compiles `src/uwl_impl.c` (`-std=c11`).
- Links the wasmtime **import library** (`libwasmtime.dll.a` + `wasmtime.dll`) — NOT the static
  archive. The static `.a` pulls in the Rust std unwinder (`_Unwind_*`), which Zig's `lld` does not
  provide → link failure.
- Links the MinGW syslibs wasmtime needs: `ws2_32 bcrypt userenv ntdll ole32 Shlwapi advapi32
  kernel32 uuid`.
- `zig build run` stages `wasmtime.dll` at the repo root (via `addUpdateSourceFiles` /
  `addCopyFileToSource`) so the OS loader finds it; the example resolves `tests/math_50.wasm`
  relative to cwd (repo root).

## Build / run flow

- `bun scripts/fetch-wasmtime.ts [VERSION] [PLATFORM]` — fetch the wasmtime C API SDK into
  `vendor/`. Needs only Bun: `fetch()` downloads (3 retries) and an in-process zip reader (central
  directory; stored + deflate; no ZIP64; rejects paths escaping `vendor/`) extracts — chosen over
  shelling out because Windows has no `unzip` and `tar -C C:\…` misreads the drive letter (binaryang
  trap). Skips if `vendor/<name>/include` exists. The full zip has 232 files (incl. the static `.a`
  and a `min/` variant); a trimmed 114-file `vendor/` is what this machine had — all 114 byte-identical.
- `zig build run` — builds + runs `examples/adder.zig`. Expected output:
  ```
  adder(3, 4) = 7
  multiply(2.5, 4.0) = 10
  square(5) = 25
  ```
  **Verified 2026-06-19** (Zig 0.16.0) — matches the README. **Re-verified 2026-10-21** in a scratch
  clone on `C:` against an SDK freshly fetched by `fetch-wasmtime.ts`.
- `zig build test` — the reference suite, **11 tests** (2026-10-26): every fixture's sha256 against
  `fixtures.json` (both directions: nothing listed missing, nothing unlisted); math, booleans,
  strings (`callStr`), `init_check` (`_initialize` ran → 99, raw no-`.wit` path), `wasi_io_50`
  (`logAndDouble(21) == 42`; it prints `doubling` through WASI — expected), version pin `@9` →
  `ImportFailed`; the binding's own error paths (missing file → `ImportFailed`, unknown export →
  `CallFailed`, `callStr` on an `i32` export → `WrongResultType`); and `imports_50` through the
  RAW `uwl.c` layer with `callconv(.c)` Zig callbacks (host imports are not on `Module`). Singleton
  and pool are not tested here: they are C-only surface, tested in -c. Ends with `All 11 tests
  passed.` **Inverted 2026-10-26** (restored after each): `math_50.wasm` one byte short → 4 red
  (the hash test + the three loads of it); bool decode inverted in `call` → only booleans red;
  `callStr`'s guard deleted → test 11 panics (null unwrap). A fourth mutant, dropping only the
  `out.kind != UWL_STR` half of that guard, stays green — EQUIVALENT, not a gap: the C loader
  leaves `str` NULL for every non-string result, so the other half still fires.
  **The run step is a plain `Run` with the test binary as an argument, not `addRunArtifact`** — see
  [best-practices.md](best-practices.md) L1.
  **21 tests since 2026-11-08 (23 with the two "limitation" tests, § Known gaps)** (SPEC 4.0.0, ledger C29): `imports_50` now through `importWith`
  (plus once through the raw layer), and ten SPEC § 8 "Refusals", each asserting what
  `lastError()` names: no callbacks (`envMul` + `envAdd`); only `envMul` (`envAdd`, not `envMul`);
  a null `fn` counts as missing; `envAdd` returning no value / `2.9 (f64)` / `true (bool)` /
  `"17" (string)` / `17 (s64)` / `2147483648 (s64)` → `combine` fails naming `envAdd`, `env-add`,
  `s32`, the value; a `string`-result import refused with no `.wasm` present (so before it is
  read); typed arguments (`add("x",1)`, `add(2.7,1)`, `add(1,1<<31)`, `add(1, i64 1)`, `add(1)`,
  `greet(42)`, `isEven("4")`, `isPositive(true)`; then a good call clears the message); result
  kinds (`call(f64|bool|i64|void, "add")`, `call(i32, "multiply")`); a `.wit` that does not parse
  (malformed parameter, `u32`, empty, text outside the world, duplicate parameter; a WIT export
  the module lacks; the accepted forms — comments, `package`, CRLF, trailing comma — still load);
  a `.wit` that is a directory; an absent `.wit` → raw. Scratch files go in `tests/` and are
  removed (`git status` clean after every run). § 8's async and http rows are n/a: no async in
  the C API, local files only (C29 item 5 n/a, as -c).
  **Inverted 2026-11-08** (restored after each, header sha re-checked): the SPEC 3.0.0 header under
  the new suite → `13 passed; 0 skipped; 8 failed.`, exactly the eight loader-level refusals (each
  loads / returns 0 / says "failed to read"); result check deleted → only the S8 test red; `i64`
  sent as s32 → only the typed-argument test red; `setError` a no-op → the nine tests that read
  `lastError()` red.
- **Build cache on NTFS:** set `ZIG_LOCAL_CACHE_DIR` to a convention path on an NTFS drive (e.g.
  `C:\zig-cache\uwl-zig`) when the repo sits on exFAT; used for every build on 2026-10-26.
- **Testing scripts without touching the repo:** clone into the session scratchpad on `C:` (NTFS) and
  run there: `git -c core.longpaths=true clone -c core.longpaths=true <abs repo path> <dir>` (the
  scratchpad path is long enough to hit `Filename too long`), then copy `vendor/` in (gitignored, so
  not cloned). The `-c safe.directory=*` this needed until 2026-10-26 is no longer required — the
  system config now covers the workspace folder (ledger C9; a clone worked without it the same day).
  PowerShell 5.1's `Get-FileHash` fails on those long paths; compare trees with a Bun script instead.

## Release / publish flow (AUTHORED 2026-06-19)

The version in `build.zig.zon` (`.version`) is the **single source of truth**. Three scripts, run in
order, keep the git tag, GitHub release, and Zigistry listing consistent with it. All are Bun +
TypeScript (`bun scripts/<name>.ts`, cross-platform; since 2026-10-21 — they replaced the 2026-06-19
Bash `.sh` + Nushell `.nu` pairs). `-h`/`--help` prints each one's usage.

1. **`scripts/bump-version.ts <major|minor|patch>`** — rewrites `.version` in `build.zig.zon`
   and commits the isolated bump. The explicit `X.Y.Z` form was removed 2026-10-26 (workspace
   versioning rule: bump scripts take patch | minor | major and refuse anything else). Guards: clean
   tree, strictly-greater target, no pre-existing tag, and (since 2026-10-26) the **compatibility gate** — fresh `api-surface.txt`, a
   bump not below the floor vs the last tag, the last tag's tests green for a patch/minor
   ([compat-gate.md](compat-gate.md)). `--dry-run` / `--no-commit`.
2. **`scripts/release.ts`** — reads `.version`, verifies a clean tree + cold-cache `zig build run`
   and `zig build test`, runs `compat.ts --release` (since 2026-10-26), tags `v<version>`, pushes branch + tag to `origin`, and creates the matching **GitHub Release** via
   `gh` (skippable). Idempotent (reuses a tag only if it points at HEAD; skips an existing Release;
   degrades if `gh` is unauthenticated). Does **not** touch Zigistry. `--dry-run` / `--no-release` /
   `--no-build` / `--remote`.
3. **`scripts/publish.ts`** — the deliberate Zigistry step, decoupled from `release.ts`. **Zigistry has
   no upload/CLI publish** ([help](https://zigistry.dev/help/)): a library is indexed by giving the
   GitHub repo the topic **`zig-package`** (a program would use `Zig`), after which it (re)indexes on
   the next push, typically within a day. The script verifies the `v<version>` tag exists locally + on
   `origin`, requires an authenticated `gh`, is **idempotent** (no-op if the topic is already set),
   prompts before changing public repo metadata (`--yes` to skip), then runs `gh repo edit --add-topic
   zig-package`. `--dry-run` / `--allow-dirty` / `--skip-tag-check` / `--remote` / `--topic`.

**v1.0.0 status (re-verified 2026-10-21 against `origin`):** `v1.0.0` is an **annotated** tag — tag
object `89a6d97`, pointing at **commit `07d954a`** ("Add scripts/bump-version.sh"). (Earlier memory
called `89a6d97` the tagged commit; that hash is the tag object. Resolve with
`git rev-parse 'v1.0.0^{commit}'`.) So the tagged tree is the working library + LICENSE + cmem plus
`fetch-wasmtime.sh`, `bump-version.sh` and `release.sh` — but not `publish.sh` or the `.nu` ports.
`main` has advanced past it (`985cb5e` on `origin` before the 2026-10-21 commit) with `publish` and
the Nushell forms. The tag was intentionally **not** moved: scripts/cmem aren't part of the published
package surface, so `v1.0.0` still marks the library state. The **GitHub Release** and the **Zigistry
topic** remain for the owner to do once `gh` is authenticated (`gh` is installed but unauthenticated
on the build host, re-checked 2026-10-21). Zigistry needs only the topic + a push; the GitHub Release is
optional polish. Next real version: `bump-version` → `release` (→ `publish`) will tag at HEAD.

**v2.0.0 (2026-11-08, owner: "update the project memory files, commit, bump, then release and
push"):** the first release through the compatibility gate. `bump-version.ts major` (floor major:
SPEC 3.0.0 → 4.0.0, and v1.0.0's surface is not computable on Zig 0.17.1), then `release.ts`
with `ZIG_LOCAL_CACHE_DIR` pointed at a NEW empty directory, so its "cold-cache" build really is
cold (it only deletes the repo's own `.zig-cache`, which an NTFS cache dir bypasses). **`gh` is not
installed on the build host (2026-11-08: not on PATH in Bash or PowerShell)**, so `release.ts`
pushes the tag and skips the GitHub Release with a warning; the Release and the Zigistry topic
(`publish.ts`, needs `gh`) stay owner items. The release record (tag id, `ls-remote` read-back)
is in the memory commit after the tag.

## Known gaps / not yet done

- **No CI.** `zig build test` (since 2026-10-26) runs by hand only; nothing runs it on push or tag.
  Windows (`x86_64-mingw` SDK) is the only platform `build.zig` knows.
- **Filesystem caveat (documented in README):** on some non-NTFS volumes ("do not record ownership"),
  Zig's build runner fails a **warm-cache** build with a bare `error: Unexpected`. Workaround:
  `rm -rf .zig-cache` before building (cold-cache is reliable), or build on an NTFS volume. Reproduced
  2026-06-19.
- **Git pack growth on non-NTFS volumes (APPLIED 2026-10-21, same fix as binaryang 2026-09-16):** on a
  volume that records no ownership (exFAT), git 2.54's per-commit auto-maintenance runs the
  *geometric* strategy detached; its `multi-pack-index` rewrite races the editor's git integration,
  fails, and leaves rolled-up packs behind (`renaming pack … failed: File exists` /
  `geometric-repack failed`), so `.git` grows every commit. Durable fix: `git gc --prune=now`, then
  **`git config --local maintenance.strategy gc`** (maintenance becomes `gc --auto`, which never
  writes a MIDX). Applied here pre-emptively — the repo was still healthy (1 pack, no MIDX); after:
  1 pack / 44 KiB, `fsck --connectivity-only` clean. It lives in `.git/config`, so **a fresh clone
  needs it again**. Health check: `git count-objects -vH` — more than a handful of `packs:` means it
  is recurring.
- ~~Host import callbacks are not surfaced in the Zig API~~ — surfaced 2026-11-08
  (`Module.importWith`). Singleton and pool are still raw-`uwl.c` only.
- **An integer literal for an `s64` parameter goes as s32** (it fits) and is refused: write
  `@as(i64, 5)`. Likewise a float literal for an `f32` parameter: `@as(f32, 2.5)`. Fixing that
  needs a C API that reports an export's parameter kinds (`uwl_export_sig`-like), which loader-c
  does not have. **Pinned by two tests since 2026-11-08** (owner: "add a test for the limitation
  to check for it and catch it before it becomes an issue"): no stamped fixture has an `s64` or
  `f32` parameter, so the tests write a hand-assembled 70-byte module (`sum(i64,i64)->i64`,
  `half(f32)->f32`, bytes inline in `tests/uwl_test.zig`) + its `.wit` as scratch files. They
  assert the refusal names the parameter and type, a runtime `i32` / `f64` is never widened /
  narrowed, the `@as` forms compute (incl. values beyond s32), a literal outside s32 is s64
  unannotated, and the result kind is checked. Inverted 2026-11-08: literals always s64 → the
  s64 test red (`found 11`); float literals f32 → the f32 test red (`found 1.25`); a runtime
  `f64` narrowed to f32 → ONLY the f32 test red (no earlier test saw that); `i64.add` → `i64.sub`
  in the module → the s64 test red (`expected 11, found -1`). If the binding ever learns the WIT
  kinds, those refusal checks fail by design: update them with the README. 23 tests.
- **Version decision for SPEC 4.0.0 (2026-11-08): MAJOR, 2.0.0, agreed by the owner** ("I agree
  that next version would be 2.0.0") — the gate's floor vs `v1.0.0` is major (and v1.0.0's
  surface is not computable on Zig 0.17.1). Bumped and released 2026-11-08 (§ Release / publish
  flow); before that the version line stayed `1.0.0` (`main` was ahead of `origin` by
  the earlier unpushed `541a02e` / `7cbe498` / `e4f026a` plus this session's commits).
- **Vendored header drift:** `src/universal_wasm_loader.h` is a manual copy of upstream
  `universalWasmLoader-c`; there is no automated sync — bump it deliberately per targeted release.
