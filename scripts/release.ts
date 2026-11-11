// Release universalWasmLoader-zig: guard, verify, push `main` — and NOTHING else. CI tags.
import { rmSync } from "node:fs";
import {
  capture,
  die,
  exec,
  githubSlug,
  help,
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
Release universalWasmLoader-zig (workspace release-process.md § 5 U4, letter H33).

🔒 Owner, 2026-11-10: "the CI to check first, then push the tag, and then create
the release page from the tag, then publish". So this script makes and pushes NO
tag. It reads the version from build.zig.zon (the single source of truth, set
by bump-version.ts), refuses unless this is a releasable state, verifies the
build locally, runs the compatibility gate, then pushes \`main\` (with the bump
commit) and reads it back with \`git ls-remote\`.

That push starts .github/workflows/release.yml, which in ONE run checks the
commit, creates the tag v<version> on it, makes the GitHub Release page from the
tag, and reads the package back with \`zig fetch\` (for Zig the tag IS the
publish). The script prints where to watch it.

Guards: a clean tree (untracked files count); on \`main\`; \`<remote>/main\`, freshly
fetched, is an ancestor of HEAD (a fast-forward push, nothing to reconcile);
v<version> absent locally AND on the remote (a released version is never
re-released, a tag never moves).

Usage: bun scripts/release.ts [options]
  --no-build       Skip the cold-cache \`zig build run\` / \`zig build test\`
                   verification (the compatibility gate still runs).
  --remote <name>  Git remote to push to (default: origin).
  --dry-run        Every guard and check; push nothing.
  -h, --help       Show this help.
`;

process.chdir(ROOT);

let doBuild = true;
let remote = "origin";
let dryRun = false;

const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  const arg = args[i];
  if (arg === "--no-build") doBuild = false;
  else if (arg === "--remote") remote = args[++i] ?? "";
  else if (arg === "--dry-run") dryRun = true;
  else if (arg === "-h" || arg === "--help") help(USAGE);
  else die(`unknown argument: ${arg}`, 2);
}

const run = runner(dryRun);

// --- Version (source of truth: build.zig.zon) --------------------------------
const tag = `v${readVersion()}`;
console.log(`Releasing ${tag} (from build.zig.zon)`);

// --- Guards ------------------------------------------------------------------
if (statusShort()) {
  console.error("error: working tree is dirty (untracked files count) — commit or remove them first.");
  reportDirty();
  process.exit(1);
}

const branch = capture(["git", "rev-parse", "--abbrev-ref", "HEAD"]).out.trim();
if (branch !== "main") die(`releases go from main; HEAD is on '${branch}'. Merge into main first ("push").`);

if (!capture(["git", "fetch", "--quiet", remote, "main"]).ok) die(`git fetch ${remote} main failed.`);
const remoteMain = revParse(`${remote}/main`);
if (!remoteMain) die(`${remote}/main is unknown after the fetch.`);
if (!capture(["git", "merge-base", "--is-ancestor", remoteMain, "HEAD"]).ok) {
  die(`${remote}/main (${remoteMain}) is not an ancestor of HEAD — pull / merge first; a release push is a fast-forward.`);
}

if (tagExists(tag)) die(`tag ${tag} exists locally — ${tag} is already released (or a stray tag); bump first. CI makes release tags.`);
const onRemote = remoteCommit(remote, `refs/tags/${tag}`);
if (onRemote !== null) die(`tag ${tag} already exists on ${remote} (at ${onRemote}) — bump first; a released version is never re-released.`);
console.log(`guards: clean tree, on main, ${remote}/main is an ancestor, ${tag} absent here and on ${remote}`);

// --- Verify it builds ----------------------------------------------------------
if (doBuild) {
  console.log("Verifying build (cold cache)…");
  console.log("+ rm -rf .zig-cache");
  if (!dryRun) rmSync(".zig-cache", { recursive: true, force: true });
  run(["zig", "build", "run"]);
  run(["zig", "build", "test"]);
}

// --- Compatibility gate: .version vs the tag before it (Rules 1-3) ------------
// Runs under --dry-run too: it changes nothing, and a release it would block
// should be known before anything is pushed.
console.log("+ bun scripts/compat.ts --release");
exec(["bun", "scripts/compat.ts", "--release"]);

// --- Push main (no tag), then read it back ---------------------------------------
run(["git", "push", remote, "main"]);
if (!dryRun) {
  const head = revParse("HEAD");
  const onMain = remoteCommit(remote, "refs/heads/main");
  console.log(`+ git ls-remote ${remote}: main = ${onMain ?? "(missing)"}`);
  if (onMain !== head) die(`${remote}'s main is not HEAD (${head}).`);
}

// --- CI does the rest: say where to watch it ------------------------------------------
const slug = githubSlug(remote);
if (slug) {
  console.log(`The push starts the release workflow: checks → tag ${tag} → Release page → zig fetch read-back.`);
  console.log(`  run:  https://github.com/${slug}/actions/workflows/release.yml`);
  console.log(`  page: https://github.com/${slug}/releases/tag/${tag}  (once the run is green)`);
} else {
  console.log(`(${remote} is not a github.com remote; no release workflow runs there.)`);
}

console.log(`Done: pushed main for ${tag}${dryRun ? " (dry run: nothing pushed)" : ""}`);
