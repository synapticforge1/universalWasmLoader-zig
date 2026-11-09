//! Reference test suite (SPEC §8) for the Zig binding: `zig build test`.
//!
//! The fixtures in this directory are the workspace's stamped reference set,
//! copied unmodified from universalWasmLoader-js (see fixtures.json); the first
//! test proves every byte still matches that stamp. Paths are relative to the
//! repo root, which build.zig sets as the test's working directory.

const std = @import("std");
const uwl = @import("uwl");

const fixtures_json = @embedFile("fixtures.json");

/// Every file fixtures.json lists, embedded so the hash check needs no I/O.
const embedded = .{
    .{ "math_50.wasm", @embedFile("math_50.wasm") },
    .{ "math_50.wit", @embedFile("math_50.wit") },
    .{ "booleans_50.wasm", @embedFile("booleans_50.wasm") },
    .{ "booleans_50.wit", @embedFile("booleans_50.wit") },
    .{ "strings_50.wasm", @embedFile("strings_50.wasm") },
    .{ "strings_50.wit", @embedFile("strings_50.wit") },
    .{ "imports_50.wasm", @embedFile("imports_50.wasm") },
    .{ "imports_50.wit", @embedFile("imports_50.wit") },
    .{ "wasi_io_50.wasm", @embedFile("wasi_io_50.wasm") },
    .{ "wasi_io_50.wit", @embedFile("wasi_io_50.wit") },
    .{ "init_check.wat", @embedFile("init_check.wat") },
    .{ "init_check.wasm", @embedFile("init_check.wasm") },
};

test "fixtures match the fixtures.json stamp (sha256, every file)" {
    const parsed = try std.json.parseFromSlice(std.json.Value, std.testing.allocator, fixtures_json, .{});
    defer parsed.deinit();
    const files = parsed.value.object.get("files").?.object;

    // Same set both ways: nothing listed is missing here, nothing here is unlisted.
    try std.testing.expectEqual(files.count(), embedded.len);
    inline for (embedded) |entry| {
        var digest: [32]u8 = undefined;
        std.crypto.hash.sha2.Sha256.hash(entry[1], &digest, .{});
        const want = files.get(entry[0]) orelse {
            std.debug.print("{s} is not listed in fixtures.json\n", .{entry[0]});
            return error.TestUnexpectedResult;
        };
        try std.testing.expectEqualStrings(want.string, &std.fmt.bytesToHex(digest, .lower));
    }
}

test "math_50: numeric round-trip" {
    var m = try uwl.Module.import("tests/math_50.wasm");
    defer m.deinit();
    try std.testing.expectEqual(@as(i32, 7), try m.call(i32, "add", .{ 3, 4 }));
    try std.testing.expectApproxEqAbs(@as(f64, 10.0), try m.call(f64, "multiply", .{ 2.5, 4.0 }), 1e-9);
    try std.testing.expectEqual(@as(i32, 25), try m.call(i32, "square", .{5}));
}

test "booleans_50: bool normalization" {
    var m = try uwl.Module.import("tests/booleans_50.wasm");
    defer m.deinit();
    try std.testing.expect(try m.call(bool, "isPositive", .{1.0}));
    try std.testing.expect(!try m.call(bool, "isPositive", .{-1.0}));
    try std.testing.expect(try m.call(bool, "inRange", .{ 5.0, 0.0, 10.0 }));
    try std.testing.expect(!try m.call(bool, "inRange", .{ 11.0, 0.0, 10.0 }));
    try std.testing.expect(try m.call(bool, "isEven", .{4}));
    try std.testing.expect(!try m.call(bool, "isEven", .{3}));
}

test "strings_50: string param + return (Canonical ABI v3.0.0)" {
    const alloc = std.testing.allocator;
    var m = try uwl.Module.import("tests/strings_50.wasm");
    defer m.deinit();

    const greeting = try m.callStr(alloc, "greet", .{"World"});
    defer alloc.free(greeting);
    try std.testing.expectEqualStrings("Hello, World!", greeting);

    const shouted = try m.callStr(alloc, "shout", .{"hi"});
    defer alloc.free(shouted);
    try std.testing.expectEqualStrings("hihi", shouted);

    try std.testing.expectEqual(@as(i32, 5), try m.call(i32, "strLen", .{"hello"}));
}

fn envMul(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = n;
    _ = ud;
    return uwl.c.uwl_f64(uwl.c.uwl_as_f64(args[0]) * uwl.c.uwl_as_f64(args[1]));
}

fn envAdd(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = n;
    _ = ud;
    return uwl.c.uwl_i32(uwl.c.uwl_as_i32(args[0]) + uwl.c.uwl_as_i32(args[1]));
}

const both_callbacks = [_]uwl.HostCallback{
    .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
    .{ .name = "envAdd", .@"fn" = envAdd, .userdata = null },
};

test "imports_50: host import callbacks (importWith)" {
    var m = try uwl.Module.importWith("tests/imports_50.wasm", &both_callbacks);
    defer m.deinit();
    try std.testing.expectApproxEqAbs(@as(f64, 12.0), try m.call(f64, "scale", .{ 3.0, 4.0 }), 1e-9);
    try std.testing.expectEqual(@as(i32, 17), try m.call(i32, "combine", .{ 10, 7 }));
}

// The raw C layer still works from Zig, the escape hatch for the singleton / pool.
test "imports_50: the same through the raw uwl.c layer" {
    var err: [*c]u8 = null;
    const h = uwl.c.uwl_import("tests/imports_50.wasm", &both_callbacks, both_callbacks.len, &err);
    if (h == null) {
        std.debug.print("load failed: {s}\n", .{err});
        uwl.c.uwl_string_free(err);
        return error.TestUnexpectedResult;
    }
    var m: uwl.Module = .{ .handle = h.? };
    defer m.deinit();
    try std.testing.expectEqual(@as(i32, 17), try m.call(i32, "combine", .{ 10, 7 }));
}

// init_check.wasm has no .wit (raw-exports path); its `_initialize` sets a
// global to 99 that `getValue` reads, so 99 proves the loader invoked it.
test "init_check: SPEC §10 _initialize runs after instantiation" {
    var m = try uwl.Module.import("tests/init_check.wasm");
    defer m.deinit();
    try std.testing.expectEqual(@as(i32, 99), try m.call(i32, "getValue", .{}));
}

// wasi_io_50 imports wasi_snapshot_preview1.fd_write; without the loader's WASI
// context the call would trap, so the result is the evidence.
test "wasi_io_50: SPEC §10 WASI lets an I/O library instantiate" {
    var m = try uwl.Module.import("tests/wasi_io_50.wasm");
    defer m.deinit();
    try std.testing.expectEqual(@as(i32, 42), try m.call(i32, "logAndDouble", .{21}));
}

test "version pinning: @9 on a module with no version global is ImportFailed" {
    try std.testing.expectError(error.ImportFailed, uwl.Module.import("tests/math_50.wasm@9"));
}

test "a missing file is ImportFailed" {
    try std.testing.expectError(error.ImportFailed, uwl.Module.import("tests/no_such_module.wasm"));
}

test "an unknown export is CallFailed" {
    var m = try uwl.Module.import("tests/math_50.wasm");
    defer m.deinit();
    try std.testing.expectError(error.CallFailed, m.call(i32, "noSuchExport", .{}));
}

test "callStr on a non-string export is WrongResultType" {
    var m = try uwl.Module.import("tests/math_50.wasm");
    defer m.deinit();
    try std.testing.expectError(error.WrongResultType, m.callStr(std.testing.allocator, "add", .{ 3, 4 }));
}

// ── SPEC 4.0.0 §8 "Refusals" (workspace ledger C29) ─────────────────────────
// Each must fail, and `uwl.lastError()` must name what SPEC §8 says it names.
// Scratch files are written into tests/ beside the fixtures and removed; the
// stamped fixtures themselves are never touched.

const io = std.testing.io;
fn cwd() std.Io.Dir {
    return std.Io.Dir.cwd();
}

/// `lastError()` contains every needle (printed when it does not).
fn expectNames(needles: []const []const u8) !void {
    const msg = uwl.lastError();
    for (needles) |n| {
        if (std.mem.indexOf(u8, msg, n) == null) {
            std.debug.print("lastError() = \"{s}\"\n  missing: \"{s}\"\n", .{ msg, n });
            return error.TestUnexpectedResult;
        }
    }
}

fn expectNotNamed(needle: []const u8) !void {
    if (std.mem.indexOf(u8, uwl.lastError(), needle) != null) {
        std.debug.print("lastError() = \"{s}\"\n  must not name: \"{s}\"\n", .{ uwl.lastError(), needle });
        return error.TestUnexpectedResult;
    }
}

fn writeScratch(path: []const u8, data: []const u8) !void {
    try cwd().writeFile(io, .{ .sub_path = path, .data = data });
}

fn removeScratch(path: []const u8) void {
    cwd().deleteFile(io, path) catch {};
}

test "refusal: imports_50 with no host callbacks names envMul and envAdd" {
    try std.testing.expectError(error.ImportFailed, uwl.Module.import("tests/imports_50.wasm"));
    try expectNames(&.{ "envMul", "envAdd" });
}

test "refusal: imports_50 with only envMul names envAdd" {
    const only_mul = [_]uwl.HostCallback{.{ .name = "envMul", .@"fn" = envMul, .userdata = null }};
    try std.testing.expectError(error.ImportFailed, uwl.Module.importWith("tests/imports_50.wasm", &only_mul));
    try expectNames(&.{"envAdd"});
    try expectNotNamed("envMul");
}

test "refusal: a callback entry with a null fn counts as missing" {
    const null_add = [_]uwl.HostCallback{
        .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
        .{ .name = "envAdd", .@"fn" = null, .userdata = null },
    };
    try std.testing.expectError(error.ImportFailed, uwl.Module.importWith("tests/imports_50.wasm", &null_add));
    try expectNames(&.{"envAdd"});
}

/// envAdd returning the wrong kind; userdata points at which.
fn envAddBad(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = args;
    _ = n;
    const sel: *const u8 = @ptrCast(ud.?);
    return switch (sel.*) {
        0 => uwl.c.uwl_void(),
        1 => uwl.c.uwl_f64(2.9),
        2 => uwl.c.uwl_bool(1),
        3 => uwl.c.uwl_str("17"),
        4 => uwl.c.uwl_i64(17),
        else => uwl.c.uwl_i64(1 << 31),
    };
}

test "refusal: envAdd returning another kind fails combine(10, 7), naming callback, import, type, value" {
    // SPEC's undefined / null → no value; 2.9; true; "17"; 17n → s64 17; 2 ** 31 → s64.
    const shown = [_][]const u8{ "no value", "2.9 (f64)", "true (bool)", "\"17\" (string)", "17 (s64)", "2147483648 (s64)" };
    const sels = [_]u8{ 0, 1, 2, 3, 4, 5 };
    for (shown, &sels) |want, *sel| {
        const cbs = [_]uwl.HostCallback{
            .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
            .{ .name = "envAdd", .@"fn" = envAddBad, .userdata = @constCast(sel) },
        };
        var m = try uwl.Module.importWith("tests/imports_50.wasm", &cbs);
        defer m.deinit();
        try std.testing.expectError(error.CallFailed, m.call(i32, "combine", .{ 10, 7 }));
        try expectNames(&.{ "envAdd", "env-add", "s32", want });
    }
}

test "refusal: an import with a string result fails the load, before the .wasm is read" {
    const wit = "tests/strres_imports.wit";
    try writeScratch(wit,
        \\world imports-50 {
        \\  import env-mul: func(p0: f64, p1: f64) -> string;
        \\  import env-add: func(p0: s32, p1: s32) -> s32;
        \\  export scale: func(x: f64, factor: f64) -> f64;
        \\}
        \\
    );
    defer removeScratch(wit);
    // No tests/strres_imports.wasm exists: the refusal must come from the .wit.
    try std.testing.expectError(error.ImportFailed, uwl.Module.importWith("tests/strres_imports.wasm", &both_callbacks));
    try expectNames(&.{ "envMul", "string" });
    try expectNotNamed("failed to read");
}

test "refusal: typed-call arguments are checked, never coerced" {
    var m = try uwl.Module.import("tests/math_50.wasm");
    defer m.deinit();
    var s = try uwl.Module.import("tests/strings_50.wasm");
    defer s.deinit();
    var b = try uwl.Module.import("tests/booleans_50.wasm");
    defer b.deinit();

    try std.testing.expectError(error.CallFailed, m.call(i32, "add", .{ "x", 1 }));
    try expectNames(&.{ "add", "'a'" });
    try std.testing.expectError(error.CallFailed, m.call(i32, "add", .{ 2.7, 1 }));
    try expectNames(&.{ "add", "'a'" });
    try std.testing.expectError(error.CallFailed, m.call(i32, "add", .{ 1, 1 << 31 })); // 2 ** 31: an s64
    try expectNames(&.{ "add", "'b'" });
    const wide: i64 = 1; // an i64 is s64 even when the value fits s32: nothing narrows
    try std.testing.expectError(error.CallFailed, m.call(i32, "add", .{ 1, wide }));
    try expectNames(&.{ "add", "'b'" });
    try std.testing.expectError(error.CallFailed, m.call(i32, "add", .{1}));
    try expectNames(&.{ "add", "expects 2" });
    try std.testing.expectError(error.CallFailed, s.callStr(std.testing.allocator, "greet", .{42}));
    try expectNames(&.{ "greet", "'name'" });
    try std.testing.expectError(error.CallFailed, b.call(bool, "isEven", .{"4"}));
    try expectNames(&.{ "isEven", "'n'" });
    try std.testing.expectError(error.CallFailed, b.call(bool, "isPositive", .{true}));
    try expectNames(&.{ "isPositive", "'x'" });

    // A matching call afterwards succeeds and clears the message.
    try std.testing.expectEqual(@as(i32, 7), try m.call(i32, "add", .{ 3, 4 }));
    try std.testing.expectEqualStrings("", uwl.lastError());
}

test "refusal: a result of another kind is WrongResultType, never a zero (S8)" {
    var m = try uwl.Module.import("tests/math_50.wasm");
    defer m.deinit();
    try std.testing.expectError(error.WrongResultType, m.call(f64, "add", .{ 3, 4 }));
    try expectNames(&.{ "add", "s32", "f64" });
    try std.testing.expectError(error.WrongResultType, m.call(i32, "multiply", .{ 2.5, 4.0 }));
    try expectNames(&.{ "multiply", "f64", "s32" });
    try std.testing.expectError(error.WrongResultType, m.call(bool, "add", .{ 3, 4 }));
    try std.testing.expectError(error.WrongResultType, m.call(i64, "add", .{ 3, 4 }));
    try std.testing.expectError(error.WrongResultType, m.call(void, "add", .{ 3, 4 }));
}

test "refusal: a .wit that does not parse fails the load, naming the .wit and the fault" {
    const wasm = "tests/bad_math.wasm";
    const wit = "tests/bad_math.wit";
    try writeScratch(wasm, @embedFile("math_50.wasm"));
    defer removeScratch(wasm);
    defer removeScratch(wit);
    const cases = [_]struct { src: []const u8, fault: []const u8 }{
        .{ .src = "world m { export add: func(a: s32 b: s32) -> s32; }", .fault = "malformed parameter" },
        .{ .src = "world m { export add: func(a: u32, b: s32) -> s32; }", .fault = "unsupported WIT type \"u32\"" },
        .{ .src = "", .fault = "it is empty" },
        .{ .src = "world m { export add: func(a: s32, b: s32) -> s32; } extra", .fault = "not a WIT world" },
        .{ .src = "world m { export add: func(a: s32, a: s32) -> s32; }", .fault = "duplicate parameter" },
    };
    for (cases) |case| {
        try writeScratch(wit, case.src);
        try std.testing.expectError(error.ImportFailed, uwl.Module.import(wasm));
        try expectNames(&.{ "bad_math.wit", case.fault });
    }
    // A WIT export the module lacks fails the load, naming it.
    try writeScratch(wit, "world m { export add: func(a: s32, b: s32) -> s32; export nope: func() -> s32; }");
    try std.testing.expectError(error.ImportFailed, uwl.Module.import(wasm));
    try expectNames(&.{"nope"});
    // The accepted forms still load: comments, package, CRLF, a trailing comma, a subset.
    try writeScratch(wit, "// gen\r\npackage local:x-1;\r\nworld math-50 {\r\n  export add: func(a: s32, b: s32,) -> s32; // tail\r\n}\r\n");
    var m = try uwl.Module.import(wasm);
    defer m.deinit();
    try std.testing.expectEqual(@as(i32, 7), try m.call(i32, "add", .{ 3, 4 }));
}

test "refusal: only an ABSENT .wit is raw; an unreadable one fails the load" {
    const wasm = "tests/dir_math.wasm";
    const wit = "tests/dir_math.wit";
    try writeScratch(wasm, @embedFile("math_50.wasm"));
    defer removeScratch(wasm);
    try cwd().createDir(io, wit, .default_dir); // a .wit that exists but cannot be read
    defer cwd().deleteDir(io, wit) catch {};
    try std.testing.expectError(error.ImportFailed, uwl.Module.import(wasm));
    try expectNames(&.{"dir_math.wit"});

    // Absent: the same bytes with no .wit beside them load raw (WASM names, WASM kinds).
    const raw = "tests/raw_math.wasm";
    try writeScratch(raw, @embedFile("math_50.wasm"));
    defer removeScratch(raw);
    var r = try uwl.Module.import(raw);
    defer r.deinit();
    try std.testing.expectApproxEqAbs(@as(f64, 10.0), try r.call(f64, "multiply", .{ 2.5, 4.0 }), 1e-9);
}

// ── The literal limitation: s64 and f32 parameters ──────────────────────────
// The C API cannot report an export's parameter kinds, so a LITERAL's kind is
// chosen by its value (an integer literal is s32 when it fits, a float literal
// f64), not by the WIT. No stamped fixture has an s64 or f32 parameter, so this
// module is built here: `sum(a: i64, b: i64) -> i64` (a + b) and
// `half(x: f32) -> f32` (x * 0.5), 70 bytes, written beside its .wit and removed.
// The test pins today's contract: such a literal is REFUSED, loudly, naming the
// parameter and its type (never silently converted), and the explicit form
// works. If the binding ever learns the WIT kinds, the refusal checks below
// fail and say so; update them and the README together.

const wide_wasm = [_]u8{
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // magic, version 1
    // type section: (i64, i64) -> i64; (f32) -> f32
    0x01, 0x0c, 0x02, 0x60, 0x02, 0x7e, 0x7e, 0x01, 0x7e, 0x60, 0x01, 0x7d, 0x01, 0x7d,
    // function section: func 0 : type 0, func 1 : type 1
    0x03, 0x03, 0x02, 0x00, 0x01,
    // export section: "sum" = func 0, "half" = func 1
    0x07, 0x0e, 0x02, 0x03, 's', 'u', 'm', 0x00, 0x00, 0x04, 'h', 'a', 'l', 'f', 0x00, 0x01,
    // code section
    0x0a, 0x14, 0x02,
    0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x7c, 0x0b, // local.get 0, local.get 1, i64.add
    0x0a, 0x00, 0x20, 0x00, 0x43, 0x00, 0x00, 0x00, 0x3f, 0x94, 0x0b, // local.get 0, f32.const 0.5, f32.mul
};

const wide_wit =
    \\world wide {
    \\  export sum: func(a: s64, b: s64) -> s64;
    \\  export half: func(x: f32) -> f32;
    \\}
    \\
;

test "limitation: an s32-sized integer literal for an s64 parameter is refused, naming it" {
    const wasm = "tests/wide_params.wasm";
    const wit = "tests/wide_params.wit";
    try writeScratch(wasm, &wide_wasm);
    defer removeScratch(wasm);
    try writeScratch(wit, wide_wit);
    defer removeScratch(wit);
    var m = try uwl.Module.import(wasm);
    defer m.deinit();

    // The limitation: 5 fits s32, so it is sent as s32, and the loader refuses it.
    try std.testing.expectError(error.CallFailed, m.call(i64, "sum", .{ 5, 6 }));
    try expectNames(&.{ "sum", "'a'", "s64" });
    // The same through a runtime i32: an i32 is s32, never widened.
    const narrow: i32 = 5;
    try std.testing.expectError(error.CallFailed, m.call(i64, "sum", .{ @as(i64, 1), narrow }));
    try expectNames(&.{ "sum", "'b'", "s64" });

    // The workaround: say the type. Values beyond s32 survive both ways.
    try std.testing.expectEqual(@as(i64, 11), try m.call(i64, "sum", .{ @as(i64, 5), @as(i64, 6) }));
    try std.testing.expectEqual(@as(i64, (1 << 40) + 1), try m.call(i64, "sum", .{ @as(i64, 1 << 40), @as(i64, 1) }));
    try std.testing.expectEqual(@as(i64, -(1 << 62)), try m.call(i64, "sum", .{ @as(i64, -(1 << 61)), @as(i64, -(1 << 61)) }));
    // A literal OUTSIDE s32 is s64 already, so it needs no annotation.
    try std.testing.expectEqual(@as(i64, (1 << 40) + (1 << 33)), try m.call(i64, "sum", .{ 1 << 40, 1 << 33 }));
    // The result kind is still checked: s64 is not i32.
    try std.testing.expectError(error.WrongResultType, m.call(i32, "sum", .{ @as(i64, 1), @as(i64, 2) }));
    try expectNames(&.{ "sum", "s64", "s32" });
}

test "limitation: a float literal for an f32 parameter is refused, naming it" {
    const wasm = "tests/wide_params.wasm";
    const wit = "tests/wide_params.wit";
    try writeScratch(wasm, &wide_wasm);
    defer removeScratch(wasm);
    try writeScratch(wit, wide_wit);
    defer removeScratch(wit);
    var m = try uwl.Module.import(wasm);
    defer m.deinit();

    // The limitation: a float literal is f64.
    try std.testing.expectError(error.CallFailed, m.call(f32, "half", .{2.5}));
    try expectNames(&.{ "half", "'x'", "f32" });
    // A runtime f64 is f64, never narrowed.
    const double: f64 = 2.5;
    try std.testing.expectError(error.CallFailed, m.call(f32, "half", .{double}));
    try expectNames(&.{ "half", "'x'", "f32" });

    // The workaround: say the type.
    try std.testing.expectEqual(@as(f32, 1.25), try m.call(f32, "half", .{@as(f32, 2.5)}));
    try std.testing.expectError(error.WrongResultType, m.call(f64, "half", .{@as(f32, 2.5)}));
    try expectNames(&.{ "half", "f32", "f64" });
}

// ── A host callback that FAILS: uwl_err (C loader 2.1.0; workspace C39) ──────

/// envAdd that fails; userdata is the message (or null for "(no message)").
fn envAddFails(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = args;
    _ = n;
    return uwl.c.uwl_err(@ptrCast(ud));
}

test "a failing result-import callback fails the export with its own message" {
    const cbs = [_]uwl.HostCallback{
        .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
        .{ .name = "envAdd", .@"fn" = envAddFails, .userdata = @constCast(@ptrCast("disk on fire")) },
    };
    var m = try uwl.Module.importWith("tests/imports_50.wasm", &cbs);
    defer m.deinit();
    try std.testing.expectError(error.CallFailed, m.call(i32, "combine", .{ 10, 7 }));
    try expectNames(&.{ "envAdd", "env-add", "failed: disk on fire" });
    try expectNotNamed("expected s32"); // its own message, not a result-kind complaint
    // The other import is untouched, and the module keeps working.
    try std.testing.expectApproxEqAbs(@as(f64, 12.0), try m.call(f64, "scale", .{ 3.0, 4.0 }), 1e-9);

    const no_msg = [_]uwl.HostCallback{
        .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
        .{ .name = "envAdd", .@"fn" = envAddFails, .userdata = null },
    };
    var m2 = try uwl.Module.importWith("tests/imports_50.wasm", &no_msg);
    defer m2.deinit();
    try std.testing.expectError(error.CallFailed, m2.call(i32, "combine", .{ 10, 7 }));
    try expectNames(&.{ "envAdd", "(no message)" });
}

// A module with two VOID imports: `run` calls `ping`, then `after` (61 bytes,
// hand-assembled; no stamped fixture has a void import). A void import ignores
// its callback's value, so before uwl_err a failing `ping` could not stop the
// WASM: `after` ran anyway.
const void_wasm = [_]u8{
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // magic, version 1
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00, // type 0: () -> ()
    // imports: env.ping, env.after (type 0)
    0x02, 0x18, 0x02,
    0x03, 'e', 'n', 'v', 0x04, 'p', 'i', 'n', 'g', 0x00, 0x00,
    0x03, 'e', 'n', 'v', 0x05, 'a', 'f', 't', 'e', 'r', 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00, // func 2 : type 0
    0x07, 0x07, 0x01, 0x03, 'r', 'u', 'n', 0x00, 0x02, // export "run" = func 2
    0x0a, 0x08, 0x01, 0x06, 0x00, 0x10, 0x00, 0x10, 0x01, 0x0b, // call 0 (ping), call 1 (after)
};

const void_wit =
    \\world void-imports {
    \\  import ping: func();
    \\  import after: func();
    \\  export run: func();
    \\}
    \\
;

const PingMode = enum(u8) { ok, fail };

fn pingCb(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = args;
    _ = n;
    const mode: *const PingMode = @ptrCast(@alignCast(ud.?));
    return if (mode.* == .fail) uwl.c.uwl_err("ping refused") else uwl.c.uwl_void();
}

fn afterCb(args: [*c]const uwl.c.uwl_val_t, n: usize, ud: ?*anyopaque) callconv(.c) uwl.c.uwl_val_t {
    _ = args;
    _ = n;
    const ran: *bool = @ptrCast(@alignCast(ud.?));
    ran.* = true;
    return uwl.c.uwl_void();
}

test "a failing VOID-import callback stops the WASM at the import" {
    const wasm = "tests/void_imports.wasm";
    const wit = "tests/void_imports.wit";
    try writeScratch(wasm, &void_wasm);
    defer removeScratch(wasm);
    try writeScratch(wit, void_wit);
    defer removeScratch(wit);

    for ([_]PingMode{ .ok, .fail }) |mode_value| {
        var mode = mode_value;
        var after_ran = false;
        const cbs = [_]uwl.HostCallback{
            .{ .name = "ping", .@"fn" = pingCb, .userdata = &mode },
            .{ .name = "after", .@"fn" = afterCb, .userdata = &after_ran },
        };
        var m = try uwl.Module.importWith(wasm, &cbs);
        defer m.deinit();
        switch (mode) {
            // The control: both imports run.
            .ok => {
                try m.call(void, "run", .{});
                try std.testing.expect(after_ran);
            },
            // The failure stops the WASM: `after` never runs, and the message is ping's.
            .fail => {
                try std.testing.expectError(error.CallFailed, m.call(void, "run", .{}));
                try expectNames(&.{ "ping", "failed: ping refused" });
                try std.testing.expect(!after_ran);
            },
        }
    }
}
