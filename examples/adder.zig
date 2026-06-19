//! Example: a WASM-backed adder that looks like ordinary Zig.
//! Run from the repo root:  zig build run

const std = @import("std");
const uwl = @import("uwl");

/// Native-looking wrapper over the WASM `add` export.
fn adder(m: uwl.Module, a: i32, b: i32) !i32 {
    return m.call(i32, "add", .{ a, b });
}

pub fn main() !void {
    var m = uwl.Module.import("examples/math_50.wasm") catch {
        std.debug.print("load failed: {s}\n", .{uwl.c.uwl_last_error()});
        return;
    };
    defer m.deinit();

    std.debug.print("adder(3, 4) = {d}\n", .{try adder(m, 3, 4)});
    std.debug.print("multiply(2.5, 4.0) = {d}\n", .{try m.call(f64, "multiply", .{ 2.5, 4.0 })});
    std.debug.print("square(5) = {d}\n", .{try m.call(i32, "square", .{5})});
}
