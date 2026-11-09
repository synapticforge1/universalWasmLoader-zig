# universalWasmLoader-zig

Idiomatic **Zig** binding for the [Universal WASM Loader](https://github.com/synapticforge1/universalWasmLoader-c).
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

var m = try uwl.Module.import("tests/math_50.wasm");
defer m.deinit();

const sum = try m.call(i32, "add", .{ 3, 4 });           // 7
const prod = try m.call(f64, "multiply", .{ 2.5, 4.0 }); // 10.0
const greeting = try m.callStr(allocator, "greet", .{"World"});
defer allocator.free(greeting);                          // "Hello, World!"
```

The result type is chosen at the call site (`m.call(i32, ...)`), and each
argument's WIT type is its Zig type — nothing is converted (SPEC 4.0.0):

| Zig argument | WIT type |
| --- | --- |
| `i32`, or an integer literal that fits it | `s32` |
| `i64`, or a larger integer literal | `s64` |
| `f32` | `f32` |
| `f64`, or a float literal | `f64` |
| `bool` | `bool` |
| `[]const u8` / a string literal | `string` |

The loader checks every argument against the export's `.wit`: a wrong count or
type (an `i64` for an `s32`, `2.7` for an `s32`, `"4"` for an `s32`) is
`error.CallFailed`, and a result of another kind than `T` is
`error.WrongResultType` — never a zero. `uwl.lastError()` names the export and
the parameter.

**Literals for `s64` and `f32` parameters need their type.** A literal's kind
comes from its value, not the `.wit` (the C loader cannot report an export's
parameter types): `5` is `s32` and `2.5` is `f64`, so `sum(5, 6)` on an `s64`
export and `half(2.5)` on an `f32` one are refused, naming the parameter. Write
`m.call(i64, "sum", .{ @as(i64, 5), @as(i64, 6) })` and
`m.call(f32, "half", .{@as(f32, 2.5)})`. A tested contract (`zig build test`,
the two "limitation" tests), not an accident.

## Build & run

```sh
bun scripts/fetch-wasmtime.ts   # downloads the wasmtime C API SDK into vendor/ (gitignored)
zig build run                   # builds and runs examples/adder.zig
zig build test                  # the reference test suite over tests/
```

`zig build run` prints:

```
adder(3, 4) = 7
multiply(2.5, 4.0) = 10
square(5) = 25
```

## API

- `uwl.Module.import(path: [:0]const u8) !Module` — load a module that imports
  nothing (a `@N` path suffix pins the version).
- `uwl.Module.importWith(path, callbacks: []const uwl.HostCallback) !Module` —
  load a module with host functions for its WIT imports. **Every** WIT import
  needs one; a missing one fails the load, naming all of them. A callback is a
  `callconv(.c)` function that must return the import's WIT result kind
  (`uwl.c.uwl_i32(...)` for `s32`); another kind fails the export call that
  reached it.
- `m.call(comptime T, name, args) !T` — call an export returning
  `i32`/`i64`/`f32`/`f64`/`bool`/`void`. `args` is a tuple, e.g. `.{ 3, 4 }`.
- `m.callStr(allocator, name, args) ![]u8` — call a string-returning export; the
  result is owned by the caller (`allocator.free`).
- `m.deinit()` — free the module.
- `uwl.lastError() []const u8` — the loader's message for this thread's last
  failed call (`""` after a success).

```zig
fn envAdd(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = n;
    _ = ud;
    return uwl.c.uwl_i32(uwl.c.uwl_as_i32(args[0]) + uwl.c.uwl_as_i32(args[1]));
}

var m = try uwl.Module.importWith("tests/imports_50.wasm", &.{
    .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
    .{ .name = "envAdd", .@"fn" = envAdd, .userdata = null },
});
```

The companion `.wit` is read first. Only an **absent** one gives raw exports
(called by their WASM names); a `.wit` that cannot be read, or does not parse
(an unsupported type such as `u32`, a malformed parameter, a WIT export the
module lacks), fails the load naming the `.wit` and the fault.

Failures surface as Zig errors (`error.ImportFailed`, `error.CallFailed`,
`error.WrongResultType`, `error.OutOfMemory`) with the message in
`uwl.lastError()`. The raw C API is reachable as `uwl.c` (the singleton and the
instance pool live there).

## How it links

The binding compiles the loader (`src/uwl_impl.c`) and links the wasmtime
**import library** (`libwasmtime.dll.a` + `wasmtime.dll`). The static archive is
intentionally not used: it pulls in the Rust std unwinder (`_Unwind_*`), which
Zig's `lld` does not provide. `zig build` ships `wasmtime.dll` next to the binary.

## Requirements & notes

- **Zig 0.17.1+** (the build uses Zig 0.17's `addTranslateC` for the C header,
  so Zig 0.16 no longer builds it), and a vendored wasmtime SDK (via `bun scripts/fetch-wasmtime.ts`;
  the repo's scripts need only [Bun](https://bun.sh)).
- The vendored C loader header lives at `src/universal_wasm_loader.h`; keep it in
  sync with the upstream [`universalWasmLoader-c`](https://github.com/synapticforge1/universalWasmLoader-c)
  release you target.
- **Filesystem caveat:** on some non-NTFS volumes (e.g. drives that "do not
  record ownership"), Zig's build runner can fail a *warm-cache* build with a
  bare `error: Unexpected`. Workaround: `rm -rf .zig-cache` before building, or
  build on an NTFS volume. A cold-cache `zig build run` is reliable.

## Versioning & breaking changes

Releases follow [Semantic Versioning 2.0.0](https://semver.org). The public surface — every
public declaration of the `uwl` module, the raw `uwl.c` layer it re-exports, and the loader SPEC
version it conforms to — is snapshotted in [`api-surface.txt`](api-surface.txt), and a release is
gated against it: anything removed or changed requires a major version, anything added at least a
minor, and a patch or minor release must also pass the previous release's test suite unmodified.
Removals are deprecated for at least one minor release first and listed below.

### ⚠️ Breaking Changes

| Version | Change | Migration |
| --- | --- | --- |
| 2.0.0 (unreleased) | SPEC 4.0.0, from C loader 2.0.0: only an absent `.wit` is raw; a `.wit` must parse strictly; every WIT import needs a callback and its return kind is checked; typed-call arguments are checked, never coerced — an `i64` is no longer narrowed to `s32`, nor an integer widened to `f64`; `call(T, …)` refuses a result of another kind. | Pass `i32` for `s32` and `f64` (or a float literal, `4.0`) for `f64`; load modules with imports through `importWith`; read `uwl.lastError()` for the reason. |
| 2.0.0 (unreleased) | Requires Zig 0.17.1 (`@cImport` is gone; the header is translated by `build.zig`). | Build with Zig 0.17.1. |

## License

MIT — see [LICENSE](LICENSE).
