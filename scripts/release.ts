// Cut a release of universalWasmLoader-zig consistently.
import { rmSync } from "node:fs";
import {
  capture,
  die,
  exec,
  githubSlug,
  help,
  ownerItem,
  readVersion,
  remoteCommit,
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
and tag to \`origin\`, and reads both back with \`git ls-remote\` (the tag must
point at HEAD there too). Zigistry / the Zig package manager resolve the package
by that tag's source tarball, so the tag is the release. A GitHub Release page
is optional polish and a web-UI task: the script prints its URL as an OWNER
ITEM (no GitHub CLI, workspace Tooling rule 9).

Usage: bun scripts/release.ts [options]
  --notes <text>   Notes to paste into the Release page (printed with its URL).
  --no-release     Do not print the Release-page owner item.
  --no-build       Skip the cold-cache \`zig build run\` / \`zig build test\`
                   verification (the compatibility gate still runs).
  --remote <name>  Git remote to push to (default: origin).
  --dry-run        Print what would happen; make no tags or pushes.
  -h, --help       Show this help.

Idempotent: if the tag already exists it is reused (must point at HEAD).
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
  run(["zig", "build", "test"]);
}

// --- Compatibility gate: .version vs the tag before it (Rules 1-3) ----------
// Runs under --dry-run too: it changes nothing, and a release it would block
// should be known before anything is tagged.
console.log("+ bun scripts/compat.ts --release");
exec(["bun", "scripts/compat.ts", "--release"]);

// --- Tag, push, release -----------------------------------------------------
if (!tagIsThere) run(["git", "tag", "-a", tag, "-m", `universalWasmLoader-zig ${tag}`]);

run(["git", "push", remote, branch]);
run(["git", "push", remote, tag]);

// --- Read back what the remote holds (check the repo, never trust the push) --
if (!dryRun) {
  const head = revParse("HEAD");
  const onBranch = remoteCommit(remote, `refs/heads/${branch}`);
  const onTag = remoteCommit(remote, `refs/tags/${tag}`);
  console.log(`+ git ls-remote ${remote}: ${branch} = ${onBranch ?? "(missing)"}, ${tag}^{} = ${onTag ?? "(missing)"}`);
  if (onTag !== head) die(`${remote} does not hold ${tag} at HEAD (${head}).`);
  if (onBranch !== head) die(`${remote}'s ${branch} is not HEAD (${head}).`);
}

if (makeRelease) {
  const slug = githubSlug(remote);
  const body = notes ||
    `Release ${tag} of universalWasmLoader-zig — idiomatic Zig binding for the Universal WASM Loader.`;
  if (slug) {
    ownerItem(`optional Release page for ${tag} (the pushed tag is the release)`,
      `https://github.com/${slug}/releases/new?tag=${tag}`, [`title: ${tag}`, `notes: ${body}`]);
  } else {
    console.log(`(${remote} is not a github.com remote; no Release page to suggest.)`);
  }
}

console.log(`Done: ${tag}`);
