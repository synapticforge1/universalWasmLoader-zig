const std = @import("std");

// Location of the wasmtime C API SDK inside this package (see
// scripts/fetch-wasmtime.sh). Only the shared bits are needed:
// include/, lib/libwasmtime.dll.a, lib/wasmtime.dll.
const wasmtime_dir = "vendor/wasmtime-v45.0.2-x86_64-mingw-c-api";

// MinGW system libraries the wasmtime import library depends on.
const win_syslibs = [_][]const u8{
    "ws2_32", "bcrypt", "userenv", "ntdll", "ole32",
    "Shlwapi", "advapi32", "kernel32", "uuid",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const wt_inc = b.path(b.pathJoin(&.{ wasmtime_dir, "include" }));
    const wt_libdir = b.path(b.pathJoin(&.{ wasmtime_dir, "lib" }));
    const wt_dll = b.path(b.pathJoin(&.{ wasmtime_dir, "lib", "wasmtime.dll" }));

    const uwl_mod = b.addModule("uwl", .{
        .root_source_file = b.path("src/uwl.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    uwl_mod.addIncludePath(b.path("src")); // vendored universal_wasm_loader.h
    uwl_mod.addIncludePath(wt_inc);
    uwl_mod.addCSourceFile(.{ .file = b.path("src/uwl_impl.c"), .flags = &.{"-std=c11"} });
    // Link the import library (shared). The static archive pulls in the Rust
    // std unwinder (_Unwind_*), which Zig's lld does not provide.
    uwl_mod.addLibraryPath(wt_libdir);
    uwl_mod.linkSystemLibrary("wasmtime", .{});
    for (win_syslibs) |lib| uwl_mod.linkSystemLibrary(lib, .{});

    const exe = b.addExecutable(.{
        .name = "adder",
        .root_module = b.createModule(.{
            .root_source_file = b.path("examples/adder.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{.{ .name = "uwl", .module = uwl_mod }},
        }),
    });
    b.installArtifact(exe);

    // `zig build run` runs from the repo root; stage wasmtime.dll there so the
    // OS loader finds it (examples/math_50.wasm resolves relative to cwd).
    const stage_dll = b.addUpdateSourceFiles();
    stage_dll.addCopyFileToSource(wt_dll, "wasmtime.dll");

    const run = b.addRunArtifact(exe);
    run.step.dependOn(&stage_dll.step);
    if (b.args) |args| run.addArgs(args);
    const run_step = b.step("run", "Run the adder example");
    run_step.dependOn(&run.step);
}
