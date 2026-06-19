//! Idiomatic Zig binding for the Universal WASM Loader.
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
//! convenience layer) so argument types are checked at comptime on the Zig
//! side. A companion `.wit` next to the `.wasm` is required for typed calls.

const std = @import("std");

pub const c = @cImport({
    @cInclude("universal_wasm_loader.h");
});

pub const Error = error{
    ImportFailed,
    CallFailed,
    WrongResultType,
    OutOfMemory,
};

/// Convert a Zig argument to a C `uwl_val_t`, comptime-dispatched per type.
fn toVal(v: anytype) c.uwl_val_t {
    const T = @TypeOf(v);
    return switch (@typeInfo(T)) {
        .int, .comptime_int => c.uwl_i64(@intCast(v)),
        .float, .comptime_float => c.uwl_f64(@floatCast(v)),
        .bool => c.uwl_bool(@intFromBool(v)),
        .pointer => c.uwl_str(@ptrCast(v)), // string literal / [*:0]const u8
        else => @compileError("unsupported argument type: " ++ @typeName(T)),
    };
}

/// Build a stack array of `uwl_val_t` from a tuple of native Zig args.
fn buildArgs(args: anytype, out: []c.uwl_val_t) void {
    const fields = @typeInfo(@TypeOf(args)).@"struct".fields;
    inline for (fields, 0..) |f, i| out[i] = toVal(@field(args, f.name));
}

/// A loaded, ABI-translated WASM module.
pub const Module = struct {
    handle: *c.uwl_module_t,

    /// Load a `.wasm` (its companion `.wit` is auto-detected). A `@N` suffix on
    /// the path pins the module version. Returns `error.ImportFailed` on failure.
    pub fn import(path: [:0]const u8) Error!Module {
        var err: [*c]u8 = null;
        const h = c.uwl_import(path.ptr, null, 0, &err);
        if (h == null) {
            if (err != null) c.uwl_string_free(err);
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
    /// values, e.g. `.{ 3, 4 }`. For string results use `callStr`.
    pub fn call(self: Module, comptime T: type, name: [:0]const u8, args: anytype) Error!T {
        const fields = @typeInfo(@TypeOf(args)).@"struct".fields;
        var vals: [fields.len]c.uwl_val_t = undefined;
        buildArgs(args, &vals);

        var out: c.uwl_val_t = undefined;
        var err: [*c]u8 = null;
        const rc = c.uwl_call(self.handle, name.ptr, &vals, fields.len, &out, &err);
        if (rc != 0) {
            if (err != null) c.uwl_string_free(err);
            return Error.CallFailed;
        }
        defer c.uwl_val_free(&out);
        return switch (T) {
            i32 => c.uwl_as_i32(out),
            i64 => c.uwl_as_i64(out),
            f32 => c.uwl_as_f32(out),
            f64 => c.uwl_as_f64(out),
            bool => c.uwl_as_bool(out) != 0,
            void => {},
            else => @compileError("unsupported result type: " ++ @typeName(T)),
        };
    }

    /// Call a string-returning export. The result is copied into `allocator`
    /// and owned by the caller (free with `allocator.free`).
    pub fn callStr(self: Module, allocator: std.mem.Allocator, name: [:0]const u8, args: anytype) Error![]u8 {
        const fields = @typeInfo(@TypeOf(args)).@"struct".fields;
        var vals: [fields.len]c.uwl_val_t = undefined;
        buildArgs(args, &vals);

        var out: c.uwl_val_t = undefined;
        var err: [*c]u8 = null;
        const rc = c.uwl_call(self.handle, name.ptr, &vals, fields.len, &out, &err);
        if (rc != 0) {
            if (err != null) c.uwl_string_free(err);
            return Error.CallFailed;
        }
        defer c.uwl_val_free(&out);
        if (out.kind != c.UWL_STR or out.str == null) return Error.WrongResultType;
        const src = out.str[0..out.len];
        return allocator.dupe(u8, src) catch Error.OutOfMemory;
    }
};
