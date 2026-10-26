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

// Host imports are not surfaced by `Module` (overview § Known gaps); the raw
// `uwl.c` layer is the documented route, so this proves it works from Zig.
test "imports_50: host import callbacks through the raw uwl.c layer" {
    var cbs = [_]uwl.c.uwl_host_callback_t{
        .{ .name = "envMul", .@"fn" = envMul, .userdata = null },
        .{ .name = "envAdd", .@"fn" = envAdd, .userdata = null },
    };
    var err: [*c]u8 = null;
    const h = uwl.c.uwl_import("tests/imports_50.wasm", &cbs, cbs.len, &err);
    if (h == null) {
        std.debug.print("load failed: {s}\n", .{err});
        uwl.c.uwl_string_free(err);
        return error.TestUnexpectedResult;
    }
    var m: uwl.Module = .{ .handle = h.? };
    defer m.deinit();
    try std.testing.expectApproxEqAbs(@as(f64, 12.0), try m.call(f64, "scale", .{ 3.0, 4.0 }), 1e-9);
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
