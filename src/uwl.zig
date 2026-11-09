//! Idiomatic Zig binding for the Universal WASM Loader (SPEC 4.0.0).
//!
//! Wraps the C single-header loader so Zig callers get error unions, native
//! types, and a comptime-typed call site instead of the C out-parameter dance:
//!
//!     const uwl = @import("uwl");
//!
//!     var m = try uwl.Module.import("tests/math_50.wasm");
//!     defer m.deinit();
//!
//!     const sum = try m.call(i32, "add", .{ 3, 4 });          // 7
//!     const s   = try m.callStr(alloc, "greet", .{"World"});   // owned []u8
//!     defer alloc.free(s);
//!
//! The wrapper targets the explicit `uwl_call` core (not the C variadic
//! convenience layer). Each argument's WIT kind is its Zig type, never a
//! conversion: `i32` is `s32`, `i64` is `s64`, `f32` / `f64`, `bool`, and a
//! string slice is `string`. An integer literal is `s32` when it fits, a float
//! literal `f64`. The loader checks every argument against the export's `.wit`
//! and refuses a mismatch (SPEC 4.0.0 §4); `call` refuses a result of another
//! kind. On any failure, `lastError()` holds the loader's message.

const std = @import("std");

/// The raw C header, translated by `build.zig` (`addTranslateC`).
pub const c = @import("c");

pub const Error = error{
    ImportFailed,
    CallFailed,
    WrongResultType,
    OutOfMemory,
};

/// A host function for one WIT import: `name` is the camelCase WIT name
/// (`env-mul` → `"envMul"`), `fn` a `callconv(.c)` function returning a value
/// of the import's WIT result kind (`c.uwl_i32(...)` for `s32`, …). To FAIL,
/// return `c.uwl_err("why")` (C loader 2.1.0): for any import, void ones
/// included, the WASM stops there and the export's call is `error.CallFailed`
/// with `lastError()` = `host callback <name> (WIT import "<name>") failed: why`.
pub const HostCallback = c.uwl_host_callback_t;

// ── The last failure's message (per thread) ─────────────────────────────────

threadlocal var last_error_len: usize = 0;
threadlocal var last_error_tl: [1024]u8 = undefined;

/// The message of the last failed `import` / `call` / `callStr` on this
/// thread (the loader's own text: it names the `.wit`, the export, the
/// parameter, the callback, …), or `""` after a success. Truncated to 1 KiB.
/// Valid until this thread's next binding call.
pub fn lastError() []const u8 {
    return last_error_tl[0..last_error_len];
}

fn clearError() void {
    last_error_len = 0;
}

fn setError(msg: []const u8) void {
    const n = @min(msg.len, last_error_tl.len);
    @memcpy(last_error_tl[0..n], msg[0..n]);
    last_error_len = n;
}

/// Take a C error string: keep its text in `lastError()`, free it.
fn takeError(err: [*c]u8, fallback: []const u8) void {
    if (err != null) {
        setError(std.mem.span(err));
        c.uwl_string_free(err);
    } else setError(fallback);
}

// ── Arguments ───────────────────────────────────────────────────────────────

/// Convert a Zig argument to a C `uwl_val_t` of the WIT kind its type names.
/// A string is COPIED by the loader (`uwl_strn`); `freeArgs` releases it.
fn toVal(v: anytype) c.uwl_val_t {
    const T = @TypeOf(v);
    switch (@typeInfo(T)) {
        .comptime_int => {
            if (v >= std.math.minInt(i32) and v <= std.math.maxInt(i32)) return c.uwl_i32(v);
            if (v >= std.math.minInt(i64) and v <= std.math.maxInt(i64)) return c.uwl_i64(v);
            @compileError(std.fmt.comptimePrint("integer literal {d} is outside s64", .{v}));
        },
        .comptime_float => return c.uwl_f64(v),
        .bool => return c.uwl_bool(@intFromBool(v)),
        else => {},
    }
    if (T == i32) {
        return c.uwl_i32(v);
    } else if (T == i64) {
        return c.uwl_i64(v);
    } else if (T == f32) {
        return c.uwl_f32(v);
    } else if (T == f64) {
        return c.uwl_f64(v);
    } else if (comptime isString(T)) {
        const s: []const u8 = v;
        return c.uwl_strn(s.ptr, s.len);
    } else {
        @compileError("unsupported argument type " ++ @typeName(T) ++
            ": pass i32 (WIT s32), i64 (s64), f32, f64, bool, or a []const u8 string — nothing is converted");
    }
}

/// A type that coerces to `[]const u8`: a slice, sentinel slice, or a pointer
/// to a byte array (a string literal).
fn isString(comptime T: type) bool {
    const info = @typeInfo(T);
    if (info != .pointer) return false;
    const p = info.pointer;
    return switch (p.size) {
        .slice => p.child == u8,
        .one => switch (@typeInfo(p.child)) {
            .array => |a| a.child == u8,
            else => false,
        },
        else => false,
    };
}

/// Build a stack array of `uwl_val_t` from a tuple of native Zig args.
fn buildArgs(args: anytype, out: []c.uwl_val_t) void {
    const names = @typeInfo(@TypeOf(args)).@"struct".field_names;
    inline for (names, 0..) |n, i| out[i] = toVal(@field(args, n));
}

fn freeArgs(vals: []c.uwl_val_t) void {
    for (vals) |*v| c.uwl_val_free(v);
}

// ── Results ─────────────────────────────────────────────────────────────────

fn kindOf(comptime T: type) c_int {
    return switch (T) {
        i32 => c.UWL_I32,
        i64 => c.UWL_I64,
        f32 => c.UWL_F32,
        f64 => c.UWL_F64,
        bool => c.UWL_BOOL,
        void => c.UWL_VOID,
        else => @compileError("unsupported result type " ++ @typeName(T) ++ ": i32, i64, f32, f64, bool or void (callStr for a string)"),
    };
}

fn kindName(kind: c_uint) []const u8 {
    return switch (kind) {
        c.UWL_VOID => "no value",
        c.UWL_I32 => "s32",
        c.UWL_I64 => "s64",
        c.UWL_F32 => "f32",
        c.UWL_F64 => "f64",
        c.UWL_BOOL => "bool",
        c.UWL_STR => "string",
        c.UWL_ERR => "an error",
        else => "an unknown kind",
    };
}

fn wrongResult(name: []const u8, got: c_uint, want: []const u8) Error {
    var buf: [256]u8 = undefined;
    setError(std.fmt.bufPrint(&buf, "export '{s}' returned {s}, not {s}", .{ name, kindName(got), want }) catch "export returned another kind");
    return Error.WrongResultType;
}

/// A loaded, ABI-translated WASM module.
pub const Module = struct {
    handle: *c.uwl_module_t,

    /// Load a `.wasm` that imports nothing. Its companion `.wit` is read
    /// first: only an ABSENT one gives raw exports; an unreadable or malformed
    /// one fails the load. A `@N` suffix on the path pins the module version.
    /// A module with WIT imports needs `importWith`.
    pub fn import(path: [:0]const u8) Error!Module {
        return importWith(path, &.{});
    }

    /// Load a `.wasm` with host callbacks for its WIT imports. EVERY WIT
    /// import needs one; a missing one fails the load, naming all of them.
    /// Callbacks for names the `.wit` does not import are ignored.
    pub fn importWith(path: [:0]const u8, callbacks: []const HostCallback) Error!Module {
        clearError();
        var err: [*c]u8 = null;
        const h = c.uwl_import(path.ptr, if (callbacks.len == 0) null else callbacks.ptr, callbacks.len, &err);
        if (h == null) {
            takeError(err, "uwl_import failed");
            return Error.ImportFailed;
        }
        return .{ .handle = h.? };
    }

    pub fn deinit(self: *Module) void {
        c.uwl_free(self.handle);
        self.handle = undefined;
    }

    /// Call an export and return its result as `T`
    /// (`i32`/`i64`/`f32`/`f64`/`bool`/`void`). `args` is a tuple of native
    /// values, e.g. `.{ 3, 4 }`. A result of another kind is
    /// `error.WrongResultType`, never a zero. For string results use `callStr`.
    pub fn call(self: Module, comptime T: type, name: [:0]const u8, args: anytype) Error!T {
        const want = comptime kindOf(T);
        var out = try self.raw(name, args);
        defer c.uwl_val_free(&out);
        if (out.kind != want) return wrongResult(name, out.kind, comptime kindName(want));
        return switch (T) {
            i32 => c.uwl_as_i32(out),
            i64 => c.uwl_as_i64(out),
            f32 => c.uwl_as_f32(out),
            f64 => c.uwl_as_f64(out),
            bool => c.uwl_as_bool(out) != 0,
            void => {},
            else => unreachable,
        };
    }

    /// Call a string-returning export. The result is copied into `allocator`
    /// and owned by the caller (free with `allocator.free`).
    pub fn callStr(self: Module, allocator: std.mem.Allocator, name: [:0]const u8, args: anytype) Error![]u8 {
        var out = try self.raw(name, args);
        defer c.uwl_val_free(&out);
        if (out.kind != c.UWL_STR or out.str == null) return wrongResult(name, out.kind, "string");
        return allocator.dupe(u8, out.str[0..out.len]) catch Error.OutOfMemory;
    }

    /// `uwl_call` with the tuple's values; the caller frees the result.
    fn raw(self: Module, name: [:0]const u8, args: anytype) Error!c.uwl_val_t {
        clearError();
        const n = @typeInfo(@TypeOf(args)).@"struct".field_names.len;
        var vals: [n]c.uwl_val_t = undefined;
        buildArgs(args, &vals);
        defer freeArgs(&vals);

        var out = c.uwl_void();
        var err: [*c]u8 = null;
        if (c.uwl_call(self.handle, name.ptr, &vals, n, &out, &err) != 0) {
            c.uwl_val_free(&out);
            takeError(err, "uwl_call failed");
            return Error.CallFailed;
        }
        return out;
    }
};
