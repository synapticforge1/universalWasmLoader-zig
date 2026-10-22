// Bump the project version in build.zig.zon (the single source of truth).
import { readFileSync, writeFileSync } from "node:fs";
import {
  die,
  help,
  readVersion,
  reportDirty,
  ROOT,
  runner,
  statusShort,
  tagExists,
  VERSION_RE,
  ZON,
} from "./_lib.ts";

const USAGE = `
Bump the project version in build.zig.zon (the single source of truth).

Updates \`.version\` to the next semver, commits the one-line change, and stops
there — tagging/pushing/releasing is scripts/release.ts's job. Typical flow:

    bun scripts/bump-version.ts minor   # 1.0.0 -> 1.1.0, commits the bump
    bun scripts/release.ts              # tags v1.1.0, pushes, GitHub Release

Usage: bun scripts/bump-version.ts <major|minor|patch|X.Y.Z> [options]
  major|minor|patch   Increment that semver component (resets lower ones).
  X.Y.Z               Set an explicit version (must be valid semver).
  --no-commit         Edit build.zig.zon only; don't commit.
  --dry-run           Print the new version + planned actions; change nothing.
  -h, --help          Show this help.

Refuses to run on a dirty tree (so the bump lands as an isolated commit) and
rejects a target that isn't strictly greater than the current version or whose
tag already exists.
`;

process.chdir(ROOT);

const SEMVER = /^\d+\.\d+\.\d+$/;
let doCommit = true;
let dryRun = false;
let spec = "";

for (const arg of process.argv.slice(2)) {
  if (arg === "--no-commit") doCommit = false;
  else if (arg === "--dry-run") dryRun = true;
  else if (arg === "-h" || arg === "--help") help(USAGE);
  else if (arg === "major" || arg === "minor" || arg === "patch") spec = arg;
  else if (/^\d.*\.\d.*\.\d/.test(arg)) spec = arg;
  else die(`unknown argument: ${arg}`, 2);
}
if (!spec) die("missing bump spec (major|minor|patch|X.Y.Z). See --help.", 2);

const run = runner(dryRun);

// --- Current version --------------------------------------------------------
const cur = readVersion();
if (!SEMVER.test(cur)) die(`current .version in ${ZON} is not plain semver: '${cur}'`);
const [ma, mi, pa] = cur.split(".").map(Number);

// --- Compute target ---------------------------------------------------------
let next: string;
if (spec === "major") next = `${ma + 1}.0.0`;
else if (spec === "minor") next = `${ma}.${mi + 1}.0`;
else if (spec === "patch") next = `${ma}.${mi}.${pa + 1}`;
else if (SEMVER.test(spec)) next = spec;
else die(`'${spec}' is not valid semver (X.Y.Z).`, 2);

// --- Validate (strictly greater, tag free) ----------------------------------
const cmp = (a: string, b: string): number => {
  const x = a.split(".").map(Number);
  const y = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
};
if (cmp(next, cur) <= 0) die(`target ${next} is not greater than current ${cur}.`);
if (tagExists(`v${next}`)) die(`tag v${next} already exists.`);

console.log(`Bumping version: ${cur} -> ${next}`);

// --- Preflight: clean tree --------------------------------------------------
if (doCommit && !dryRun && statusShort()) {
  console.error("error: working tree is dirty — commit or stash so the bump is isolated.");
  reportDirty();
  process.exit(1);
}

// --- Apply ------------------------------------------------------------------
if (!dryRun) {
  // Replace only the .version value, preserving formatting.
  writeFileSync(ZON, readFileSync(ZON, "utf8").replace(VERSION_RE, `$1${next}$3`));
}
console.log(`+ set .version = "${next}" in ${ZON}`);

if (doCommit) {
  run(["git", "add", ZON]);
  run(["git", "commit", "-m", `Bump version: v${cur} -> v${next}`]);
}

console.log(`Done: ${next}`);
if (doCommit) {
  console.log(`Next: bun scripts/release.ts   # tags v${next}, pushes, creates the GitHub Release`);
}
