# cmem — Portable Project Memory for universalWasmLoader-zig

This folder is the **authoritative, portable project memory** for `universalWasmLoader-zig`. It lives
inside the project tree, so it travels with the repo and is committed to git. Keep files small and
single-topic.

## Policy

- **`cmem/` is the single home for ALL project memory.** When the owner says "**update the project
  memory**," update the matching `cmem/` topic file with the latest decisions, found bugs, design
  changes, and current state — then add/refresh its one-line pointer in the table below. Convert
  relative dates to absolute; update existing entries rather than duplicating.
- **`README` is NOT project memory.** It is the public, user-facing doc. Keep internal decision logs /
  bug post-mortems out of it — those live here in `cmem/`.

### The "update the project memory" trigger (binding on every agent)
When the owner says **"update the project memory"** (or a synonym — "update memory", "record this"),
do BOTH: (1) revise all relevant `cmem/` files and refresh the Files-table pointers (absolute dates;
update, don't duplicate); and (2) sync the user-facing README only where the change is user-relevant —
never copy internal decision logs into it.

### The "look for code issues" trigger (binding on every agent)
When the owner says **"look for code issues"** (or "code audit" / "audit the code"), perform a
comprehensive audit of the Zig binding (`src/uwl.zig`) and its C-link glue (`src/uwl_impl.c`,
`build.zig`) for: (1) workarounds / temporary hacks; (2) dead code; (3) bugs (wrong comptime type
dispatch in `toVal`/`call`, leaked C-owned strings, missing `uwl_val_free`/`uwl_string_free`, missing
`null` checks on `[*c]` returns); and (4) silent fall-throughs (returning a default instead of
erroring). Report `file:line` + severity, fix the safe ones, and confirm `zig build run` still prints
the expected adder output.

## Files

| File | What it holds |
| --- | --- |
| [overview.md](overview.md) | What this Zig port is, its language/runtime, API surface, how it links the C loader + wasmtime, build/run flow, and known gaps |
