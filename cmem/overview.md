# overview — universalWasmLoader-zig

## What this is

`universalWasmLoader-zig` is the **idiomatic Zig binding** of the Universal WASM Loader family. It is
a thin, zero-duplication wrapper over the C single-header loader
([`universalWasmLoader-c`](https://github.com/synapticforge1/universalWasmLoader-c)) — the Zig sibling of
the reference JS loader [`@jrmarcum/universal-wasm-loader`](https://jsr.io/@jrmarcum/universal-wasm-loader).
The hard parts (WIT parsing, Canonical ABI marshalling, `cabi_post` string returns, version pinning)
live **once** in the C loader; this repo only adds a Zig-native call surface on top.

- **Language / runtime:** Zig **0.16+** (`minimum_zig_version = "0.16.0"`), built on the
  [wasmtime C API](https://docs.wasmtime.dev/c-api/) (v45.0.2) as the WASM engine.
- **What it gives Zig callers:** error unions instead of out-params, native Zig argument/result types,
  and a **comptime-typed call site** (`m.call(i32, "add", .{3,4})`) — no manual `uwl_val_t` boxing.
- **Distribution:** a Zig module (`build.zig.zon` name `universal_wasm_loader`, v1.0.0, MIT) exposing
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

## Current state — IMPLEMENTED v1 (2026-06-19)

Working tree:

- `src/uwl.zig` — the binding (public API + comptime arg/result dispatch). Imports the C header via
  `@cImport` and re-exports it as `uwl.c` (raw escape hatch).
- `src/uwl_impl.c` — 3-line TU that `#define UWL_IMPLEMENTATION` + includes the header to compile the
  loader body into the module.
- `src/universal_wasm_loader.h` — **vendored copy** of the upstream C single-header loader. Keep in
  sync with the `universalWasmLoader-c` release being targeted.
- `examples/adder.zig` — the runnable example (`zig build run`). `examples/math_50.wasm` +
  `math_50.wit` are the reference fixture.
- `build.zig` — compiles the C loader, links wasmtime, stages `wasmtime.dll`, defines `run` step.
- `scripts/fetch-wasmtime.sh` — downloads the wasmtime C API SDK into `vendor/` (gitignored).
- `scripts/bump-version.{sh,nu}` / `release.{sh,nu}` / `publish.{sh,nu}` — the release toolchain, in
  Bash and equivalent cross-platform Nushell forms (see "Release / publish flow" below).
- `vendor/` — wasmtime C API SDK (headers + `libwasmtime.dll.a` + `wasmtime.dll`). **gitignored**;
  fetched on demand.
- `LICENSE` — MIT (Copyright (c) 2026 Jon Marcum), identical to the `-c` port. Listed in
  `build.zig.zon` `paths`; README License section links it.
- `CLAUDE.md` + `cmem/` — the auto-loaded pointer banner and this portable project memory.

Build environment used for v1: Zig 0.16.0 + wasmtime C API **v45.0.2** `x86_64-mingw`.

## Public API surface (implemented)

All in `src/uwl.zig`. Idiomatic Zig over the C loader's explicit `uwl_call` core (NOT the C variadic
convenience layer — argument types are checked at comptime on the Zig side instead).

- **`uwl.Module`** — a loaded, ABI-translated module (`handle: *c.uwl_module_t`).
- **`Module.import(path: [:0]const u8) Error!Module`** — load a `.wasm`; its companion `.wit` is
  auto-detected, a `@N` path suffix pins the version. Returns `error.ImportFailed` (frees the C error
  string). Wraps `uwl_import`.
- **`m.call(comptime T, name, args) Error!T`** — call an export returning
  `i32`/`i64`/`f32`/`f64`/`bool`/`void`. `args` is a tuple of native values (`.{3,4}`). Frees the C
  out-val via `defer uwl_val_free`. Unsupported `T` is a `@compileError`.
- **`m.callStr(allocator, name, args) Error![]u8`** — call a string-returning export; result is
  **copied into `allocator`** and owned by the caller (`allocator.free`). Returns
  `error.WrongResultType` if the export didn't return a `UWL_STR`.
- **`m.deinit()`** — `uwl_free` + poisons the handle.
- **`uwl.Error`** = `{ ImportFailed, CallFailed, WrongResultType, OutOfMemory }`.
- **`uwl.c`** — the raw `@cImport` namespace (e.g. `uwl.c.uwl_last_error()` for the underlying
  message). The escape hatch for raw (no-`.wit`) modules / anything not surfaced idiomatically.

### Arg/result marshalling (comptime)

`toVal(v)` dispatches on `@typeInfo(@TypeOf(v))`: int→`uwl_i64`, float→`uwl_f64`, bool→`uwl_bool`,
pointer→`uwl_str` (string literal / `[*:0]const u8`). `buildArgs` `inline for`s the args tuple into a
stack `[N]uwl_val_t`. The C loader re-narrows each value to the export's actual WIT type, so passing
`i64`/`f64` for narrower params is fine. **A companion `.wit` is required** (source of per-arg types).

## How it links

- `build.zig` adds the module `uwl` with `link_libc = true`, include paths for `src/` (vendored
  header) and the wasmtime SDK `include/`, and compiles `src/uwl_impl.c` (`-std=c11`).
- Links the wasmtime **import library** (`libwasmtime.dll.a` + `wasmtime.dll`) — NOT the static
  archive. The static `.a` pulls in the Rust std unwinder (`_Unwind_*`), which Zig's `lld` does not
  provide → link failure.
- Links the MinGW syslibs wasmtime needs: `ws2_32 bcrypt userenv ntdll ole32 Shlwapi advapi32
  kernel32 uuid`.
- `zig build run` stages `wasmtime.dll` at the repo root (via `addUpdateSourceFiles` /
  `addCopyFileToSource`) so the OS loader finds it; the example resolves `examples/math_50.wasm`
  relative to cwd (repo root).

## Build / run flow

- `scripts/fetch-wasmtime.sh` — fetch the wasmtime C API SDK into `vendor/` (needs `curl`, `unzip`).
- `zig build run` — builds + runs `examples/adder.zig`. Expected output:
  ```
  adder(3, 4) = 7
  multiply(2.5, 4.0) = 10
  square(5) = 25
  ```
  **Verified 2026-06-19** (Zig 0.16.0) — matches the README.

## Release / publish flow (AUTHORED 2026-06-19)

The version in `build.zig.zon` (`.version`) is the **single source of truth**. Three scripts, run in
order, keep the git tag, GitHub release, and Zigistry listing consistent with it. Each ships in **two
equivalent forms** (same flags/guards/behavior): a Bash `.sh` and a cross-platform Nushell `.nu`
(added 2026-06-19, needs `nu` ≥0.113; runs on Windows/macOS/Linux without Git Bash). Use whichever the
machine has — e.g. `nu scripts/release.nu` or `bash scripts/release.sh`. `fetch-wasmtime.sh` stays
Bash-only (one-time dev setup).

1. **`scripts/bump-version.sh <major|minor|patch|X.Y.Z>`** — rewrites `.version` in `build.zig.zon`
   and commits the isolated bump. Guards: clean tree, strictly-greater target, no pre-existing tag,
   valid semver. `--dry-run` / `--no-commit`.
2. **`scripts/release.sh`** — reads `.version`, verifies a clean tree + cold-cache `zig build run`,
   tags `v<version>`, pushes branch + tag to `origin`, and creates the matching **GitHub Release** via
   `gh` (skippable). Idempotent (reuses a tag only if it points at HEAD; skips an existing Release;
   degrades if `gh` is unauthenticated). Does **not** touch Zigistry. `--dry-run` / `--no-release` /
   `--no-build` / `--remote`.
3. **`scripts/publish.sh`** — the deliberate Zigistry step, decoupled from `release.sh`. **Zigistry has
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

## Known gaps / not yet done

- **No test suite.** Unlike `-c` (`spec_tests.c`, 29 assertions), this repo only ships the `adder`
  example as a smoke test. A `zig build test` over the SPEC §8 fixtures is not yet authored.
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
- **Host import callbacks** are not surfaced in the Zig API — `import` passes `null, 0` for callbacks.
  Pure-compute / partial-import modules work; modules needing host imports must use the raw `uwl.c`
  layer.
- **String/aggregate args beyond flat scalars + strings** rely entirely on the C loader; no Zig-side
  validation beyond the `toVal` type switch.
- **Vendored header drift:** `src/universal_wasm_loader.h` is a manual copy of upstream
  `universalWasmLoader-c`; there is no automated sync — bump it deliberately per targeted release.
