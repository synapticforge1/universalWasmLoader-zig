> **⚠️ PORTABLE PROJECT MEMORY LIVES IN `cmem/`** — start at [`cmem/INDEX.md`](cmem/INDEX.md).

> **At session start, read `../../cmem/handoffs.md` for letters addressed to this repo.** The workspace
> session (`../../cmem/INDEX.md`) reviews across the sibling repos and reports there; nothing is ever
> written into this repo from outside. (owner directive, 2026-09-20)

> When saving new project memory, write it into the matching `cmem/` topic file (and refresh its
> pointer in `cmem/INDEX.md`). The **"update the project memory"** and **"look for code issues"**
> triggers are defined in `cmem/INDEX.md` and are binding on every agent.

# universalWasmLoader-zig

Idiomatic Zig binding for the [Universal WASM Loader](https://github.com/synapticforge1/universalWasmLoader-c).
Hosted at `github.com/synapticforge1/universalWasmLoader-zig` (moved from `jrmarcum`, 2026-10-21).
A thin, zero-duplication wrapper over the C single-header loader: call `.wasm` exports with native Zig
values, error unions, and a comptime-typed call site. See [`README.md`](README.md) for user-facing
docs and [`cmem/overview.md`](cmem/overview.md) for the project memory.

## Quick orientation

- `src/uwl.zig` — the binding (public `Module` API + comptime arg/result dispatch); re-exports the raw
  C header as `uwl.c`.
- `src/uwl_impl.c` + `src/universal_wasm_loader.h` — vendored C loader compiled into the module.
- `build.zig` — compiles the loader, links the wasmtime **import** library (not the static archive),
  stages `wasmtime.dll`, defines the `run` step.
- `scripts/fetch-wasmtime.sh` — fetches the wasmtime C API SDK into `vendor/` (gitignored).
- `zig build run` — builds + runs `examples/adder.zig`.
