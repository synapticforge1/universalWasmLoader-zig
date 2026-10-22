// Cut a release of universalWasmLoader-zig consistently.
import { rmSync } from "node:fs";
import {
  capture,
  die,
  has,
  help,
  readVersion,
  reportDirty,
  revParse,
  ROOT,
  runner,
  statusShort,
  tagExists,
} from "./_lib.ts";

const USAGE = `
Cut a release of universalWasmLoader-zig consistently.

The single source of truth for the version is \`.version\` in build.zig.zon.
This script reads it, tags the current commit \`v<version>\`, pushes the branch
and tag to \`origin\`, and (unless --no-release) creates the matching GitHub
Release. Zigistry / the Zig package manager resolve the package by that tag's
source tarball, so the tag is the part that actually matters; the GitHub
Release is human-readable polish.

Usage: bun scripts/release.ts [options]
  --notes <text>   Release notes body (default: a generic line).
  --no-release     Push the tag only; skip creating a GitHub Release.
  --no-build       Skip the cold-cache \`zig build run\` verification.
  --remote <name>  Git remote to push to (default: origin).
  --dry-run        Print what would happen; make no tags/pushes/releases.
  -h, --help       Show this help.

Idempotent: if the tag already exists it is reused (must point at HEAD); an
existing GitHub Release is left untouched.
`;

process.chdir(ROOT);

let notes = "";
let makeRelease = true;
let doBuild = true;
let remote = "origin";
let dryRun = false;

const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  const arg = args[i];
  if (arg === "--notes") notes = args[++i] ?? "";
  else if (arg === "--no-release") makeRelease = false;
  else if (arg === "--no-build") doBuild = false;
  else if (arg === "--remote") remote = args[++i] ?? "";
  else if (arg === "--dry-run") dryRun = true;
  else if (arg === "-h" || arg === "--help") help(USAGE);
  else die(`unknown argument: ${arg}`, 2);
}

const run = runner(dryRun);

// --- Derive version from build.zig.zon (source of truth) --------------------
const tag = `v${readVersion()}`;
console.log(`Releasing ${tag} (from build.zig.zon)`);

// --- Preflight checks -------------------------------------------------------
if (statusShort()) {
  console.error("error: working tree is dirty — commit or stash before releasing.");
  reportDirty();
  process.exit(1);
}

const branch = capture(["git", "rev-parse", "--abbrev-ref", "HEAD"]).out.trim();
console.log(`On branch: ${branch}`);

// If the tag already exists, it must point at the current commit.
const tagIsThere = tagExists(tag);
if (tagIsThere) {
  if (revParse(`${tag}^{commit}`) !== revParse("HEAD")) {
    console.error(`error: tag ${tag} exists but does not point at HEAD.`);
    console.error("       Bump .version in build.zig.zon or move the tag deliberately.");
    process.exit(1);
  }
  console.log(`tag ${tag} already exists at HEAD — reusing.`);
}

// --- Verify it builds -------------------------------------------------------
if (doBuild) {
  console.log("Verifying build (cold cache)…");
  console.log("+ rm -rf .zig-cache");
  if (!dryRun) rmSync(".zig-cache", { recursive: true, force: true });
  run(["zig", "build", "run"]);
}

// --- Tag, push, release -----------------------------------------------------
if (!tagIsThere) run(["git", "tag", "-a", tag, "-m", `universalWasmLoader-zig ${tag}`]);

run(["git", "push", remote, branch]);
run(["git", "push", remote, tag]);

if (makeRelease) {
  if (!has("gh")) {
    console.error("warning: gh CLI not found — skipping GitHub Release (tag is pushed).");
  } else if (capture(["gh", "release", "view", tag]).ok) {
    console.log(`GitHub Release ${tag} already exists — leaving it untouched.`);
  } else {
    const body = notes ||
      `Release ${tag} of universalWasmLoader-zig — idiomatic Zig binding for the Universal WASM Loader.`;
    run(["gh", "release", "create", tag, "--title", tag, "--notes", body]);
  }
}

console.log(`Done: ${tag}`);
