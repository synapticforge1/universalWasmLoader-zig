const std = @import("std");

// Location of the wasmtime C API SDK inside this package (see
// scripts/fetch-wasmtime.ts). Only the shared bits are needed:
// include/, lib/libwasmtime.dll.a, lib/wasmtime.dll.
const wasmtime_dir = "vendor/wasmtime-v45.0.2-x86_64-mingw-c-api";

// MinGW system libraries the wasmtime import library depends on.
const win_syslibs = [_][]const u8{
    "ws2_32", "bcrypt", "userenv", "ntdll", "ole32",
    "Shlwapi", "advapi32", "kernel32", "uuid",
};

/// Prepend `sub_path` (relative to the build root) to the run's PATH so the
/// OS loader finds wasmtime.dll. Zig 0.17 removed `Step.Run.addPathDir` and
/// `Build.pathFromRoot`; the env map is cloned from the build's environment on
/// first use, and its lookups ignore case on Windows.
fn prependPath(run: *std.Build.Step.Run, sub_path: []const u8) void {
    const b = run.step.owner;
    const dir = b.root.joinString(b.allocator, sub_path) catch @panic("OOM");
    const env = run.getEnvMap();
    const sep: []const u8 = if (@import("builtin").os.tag == .windows) ";" else ":";
    const value = if (env.get("PATH")) |old| b.fmt("{s}{s}{s}", .{ dir, sep, old }) else dir;
    run.setEnvironmentVariable("PATH", value);
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const wt_inc = b.path(b.pathJoin(&.{ wasmtime_dir, "include" }));
    const wt_libdir = b.path(b.pathJoin(&.{ wasmtime_dir, "lib" }));
    const wt_dll = b.path(b.pathJoin(&.{ wasmtime_dir, "lib", "wasmtime.dll" }));

    // The compatibility gate (scripts/compat.ts) points these at a previous
    // tag's files extracted under .compat/; everyone else uses the defaults.
    const src_dir = b.option([]const u8, "src", "Directory of the binding's source (default: src)") orelse "src";
    const test_root = b.option([]const u8, "test-root", "Test suite root file (default: tests/uwl_test.zig)") orelse "tests/uwl_test.zig";
    const test_cwd = b.option([]const u8, "test-cwd", "Directory the test suite runs in (default: the repo root)") orelse ".";

    // The C header, translated by the build (Zig 0.17 removed `@cImport`);
    // src/uwl.zig imports it as "c" and re-exports it as `uwl.c`.
    const c_header = b.addTranslateC(.{
        .root_source_file = b.path(b.pathJoin(&.{ src_dir, "universal_wasm_loader.h" })),
        .target = target,
        .optimize = optimize,
    });
    c_header.addIncludePath(wt_inc);

    const uwl_mod = b.addModule("uwl", .{
        .root_source_file = b.path(b.pathJoin(&.{ src_dir, "uwl.zig" })),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .imports = &.{.{ .name = "c", .module = c_header.createModule() }},
    });
    uwl_mod.addIncludePath(b.path(src_dir)); // vendored universal_wasm_loader.h
    uwl_mod.addIncludePath(wt_inc);
    uwl_mod.addCSourceFile(.{ .file = b.path(b.pathJoin(&.{ src_dir, "uwl_impl.c" })), .flags = &.{"-std=c11"} });
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
    // OS loader finds it (tests/math_50.wasm resolves relative to cwd).
    const stage_dll = b.addUpdateSourceFiles();
    stage_dll.addCopyFileToSource(wt_dll, "wasmtime.dll");

    const run = b.addRunArtifact(exe);
    run.step.dependOn(&stage_dll.step);
    run.addPassthruArgs(); // `zig build run -- <args>` (Zig 0.17: replaces `b.args`)
    const run_step = b.step("run", "Run the adder example");
    run_step.dependOn(&run.step);

    // `zig build test` — the SPEC §8 reference suite over tests/ (fixture
    // paths resolve from the repo root, so the run's cwd is pinned there).
    // Run as a plain artifact, NOT `addRunArtifact`: that puts the test runner
    // in `--listen=-` mode, where stdout is the build protocol's pipe, and the
    // loader's WASI context inherits stdout — wasi_io_50's `fd_write` then
    // stalls the runner forever (hung >10 min, 2026-10-26).
    const tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path(test_root),
            .target = target,
            .optimize = optimize,
            .imports = &.{.{ .name = "uwl", .module = uwl_mod }},
        }),
    });
    const run_tests = std.Build.Step.Run.create(b, "run uwl tests");
    run_tests.addArtifactArg(tests);
    run_tests.setCwd(b.path(test_cwd));
    // A cwd other than the repo root has no staged wasmtime.dll; PATH finds it.
    prependPath(run_tests, b.pathJoin(&.{ wasmtime_dir, "lib" }));
    run_tests.step.dependOn(&stage_dll.step);
    const test_step = b.step("test", "Run the reference test suite");
    test_step.dependOn(&run_tests.step);

    // `zig build surface` — print the public surface (tools/surface.zig);
    // scripts/surface.ts diffs it against the committed api-surface.txt.
    const surface_tool = b.addExecutable(.{
        .name = "surface",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tools/surface.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{.{ .name = "uwl", .module = uwl_mod }},
        }),
    });
    const run_surface = b.addRunArtifact(surface_tool);
    prependPath(run_surface, b.pathJoin(&.{ wasmtime_dir, "lib" }));
    const surface_step = b.step("surface", "Print the public API surface");
    surface_step.dependOn(&run_surface.step);
}
