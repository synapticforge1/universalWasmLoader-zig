# overview — universalWasmLoader-zig

## What this is

`universalWasmLoader-zig` is the **idiomatic Zig binding** of the Universal WASM Loader family. It is
a thin, zero-duplication wrapper over the C single-header loader
([`universalWasmLoader-c`](https://github.com/jrmarcum/universalWasmLoader-c)) — the Zig sibling of
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

## Known gaps / not yet done

- **No test suite.** Unlike `-c` (`spec_tests.c`, 29 assertions), this repo only ships the `adder`
  example as a smoke test. A `zig build test` over the SPEC §8 fixtures is not yet authored.
- **Filesystem caveat (documented in README):** on some non-NTFS volumes ("do not record ownership"),
  Zig's build runner fails a **warm-cache** build with a bare `error: Unexpected`. Workaround:
  `rm -rf .zig-cache` before building (cold-cache is reliable), or build on an NTFS volume. Reproduced
  2026-06-19.
- **Host import callbacks** are not surfaced in the Zig API — `import` passes `null, 0` for callbacks.
  Pure-compute / partial-import modules work; modules needing host imports must use the raw `uwl.c`
  layer.
- **String/aggregate args beyond flat scalars + strings** rely entirely on the C loader; no Zig-side
  validation beyond the `toVal` type switch.
- **Vendored header drift:** `src/universal_wasm_loader.h` is a manual copy of upstream
  `universalWasmLoader-c`; there is no automated sync — bump it deliberately per targeted release.
