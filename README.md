# universalWasmLoader-zig

Idiomatic **Zig** binding for the [Universal WASM Loader](https://github.com/jrmarcum/universalWasmLoader-c).
It wraps the C single-header loader so you call `.wasm` exports with native Zig
values and error unions — the Zig sibling of the reference JS loader
[`@jrmarcum/universal-wasm-loader`](https://jsr.io/@jrmarcum/universal-wasm-loader).

The hard parts (WIT parsing, Canonical ABI marshalling, `cabi_post` string
returns, version pinning) live **once**, in the C loader. This repo is a thin,
zero-duplication wrapper over it — built on the
[wasmtime C API](https://docs.wasmtime.dev/c-api/).

## Quick start

```zig
const uwl = @import("uwl");

var m = try uwl.Module.import("examples/math_50.wasm");
defer m.deinit();

const sum = try m.call(i32, "add", .{ 3, 4 });           // 7
const prod = try m.call(f64, "multiply", .{ 2.5, 4.0 }); // 10.0
const greeting = try m.callStr(allocator, "greet", .{"World"});
defer allocator.free(greeting);                          // "Hello, World!"
```

The result type is chosen at the call site (`m.call(i32, ...)`); each argument in
the tuple is read with the correct type from the export's WIT signature, so you
pass plain Zig values. A companion `.wit` next to the `.wasm` is required.

## Build & run

```sh
scripts/fetch-wasmtime.sh     # downloads the wasmtime C API SDK into vendor/ (gitignored)
zig build run                 # builds and runs examples/adder.zig
```

`zig build run` prints:

```
adder(3, 4) = 7
multiply(2.5, 4.0) = 10
square(5) = 25
```

## API

- `uwl.Module.import(path: [:0]const u8) !Module` — load a module (its `.wit` is
  auto-detected; a `@N` path suffix pins the version).
- `m.call(comptime T, name, args) !T` — call an export returning
  `i32`/`i64`/`f32`/`f64`/`bool`/`void`. `args` is a tuple, e.g. `.{ 3, 4 }`.
- `m.callStr(allocator, name, args) ![]u8` — call a string-returning export; the
  result is owned by the caller (`allocator.free`).
- `m.deinit()` — free the module.

Failures surface as Zig errors (`error.ImportFailed`, `error.CallFailed`, …). For
the underlying message, the raw C API is reachable as `uwl.c` (e.g.
`uwl.c.uwl_last_error()`).

## How it links

The binding compiles the loader (`src/uwl_impl.c`) and links the wasmtime
**import library** (`libwasmtime.dll.a` + `wasmtime.dll`). The static archive is
intentionally not used: it pulls in the Rust std unwinder (`_Unwind_*`), which
Zig's `lld` does not provide. `zig build` ships `wasmtime.dll` next to the binary.

## Requirements & notes

- **Zig 0.16+**, and a vendored wasmtime SDK (via `scripts/fetch-wasmtime.sh`).
- The vendored C loader header lives at `src/universal_wasm_loader.h`; keep it in
  sync with the upstream [`universalWasmLoader-c`](https://github.com/jrmarcum/universalWasmLoader-c)
  release you target.
- **Filesystem caveat:** on some non-NTFS volumes (e.g. drives that "do not
  record ownership"), Zig's build runner can fail a *warm-cache* build with a
  bare `error: Unexpected`. Workaround: `rm -rf .zig-cache` before building, or
  build on an NTFS volume. A cold-cache `zig build run` is reliable.

## License

MIT.
