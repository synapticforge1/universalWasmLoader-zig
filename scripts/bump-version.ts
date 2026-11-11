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
import {
  currentSurface,
  describeTests,
  floorVs,
  levelOf,
  previousTests,
  printFloor,
  rank,
  releaseTags,
} from "./compat.ts";

const USAGE = `
Bump the project version in build.zig.zon (the single source of truth).

Updates \`.version\` to the next semver, commits the one-line change, and stops
there — pushing is scripts/release.ts's job, and CI tags (H33). Typical flow:

    bun scripts/bump-version.ts minor   # 1.0.0 -> 1.1.0, commits the bump
    bun scripts/release.ts              # pushes main; CI checks, tags v1.1.0, page, read-back

Usage: bun scripts/bump-version.ts <major|minor|patch> [options]
  major|minor|patch   Increment that semver component (resets lower ones).
                      Nothing else is accepted (workspace rule: SemVer 2.0.0,
                      no hand-set versions).
  --no-commit         Edit build.zig.zon only; don't commit.
  --dry-run           Print the new version + planned actions; change nothing.
  -h, --help          Show this help.

Refuses to run on a dirty tree (so the bump lands as an isolated commit) and
rejects a target that isn't strictly greater than the current version or whose
tag already exists.

The compatibility gate (scripts/compat.ts) runs first, --dry-run included:
api-surface.txt must be current, the bump must not be below the FLOOR the
surface diff against the last release tag computes (removed/changed -> major,
added -> minor), and a patch or minor needs that tag's tests to pass against
today's source.
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
  else die(`unknown argument: ${arg} (a bump is major|minor|patch)`, 2);
}
if (!spec) die("missing bump spec (major|minor|patch). See --help.", 2);

const run = runner(dryRun);

// --- Current version --------------------------------------------------------
const cur = readVersion();
if (!SEMVER.test(cur)) die(`current .version in ${ZON} is not plain semver: '${cur}'`);
const [ma, mi, pa] = cur.split(".").map(Number);

// --- Compute target ---------------------------------------------------------
let next: string;
if (spec === "major") next = `${ma + 1}.0.0`;
else if (spec === "minor") next = `${ma}.${mi + 1}.0`;
else next = `${ma}.${mi}.${pa + 1}`;

// --- Validate (strictly greater, tag free) ----------------------------------
const cmp = (a: string, b: string): number => {
  const x = a.split(".").map(Number);
  const y = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
};
if (cmp(next, cur) <= 0) die(`target ${next} is not greater than current ${cur}.`);
if (tagExists(`v${next}`)) die(`tag v${next} already exists.`);

// --- Compatibility gate (workspace Rules 1-3) -------------------------------
const lastTag = releaseTags()[0];
if (lastTag) {
  if (`v${cur}` !== lastTag) {
    die(`the version line (${cur}) is not the last release tag (${lastTag}); a bump starts from a released version.`);
  }
  const step = levelOf(cur, next);
  const f = floorVs(lastTag, currentSurface());
  printFloor(lastTag, f);
  if (rank(step) < rank(f.floor)) die(`a ${step} bump is below the floor (${f.floor}).`);
  if (step !== "major") {
    const r = previousTests(lastTag);
    console.log(describeTests(lastTag, r));
    if (!r.ok) {
      console.error(r.output);
      die(`a ${step} bump requires the previous tag's tests to pass.`);
    }
  }
} else {
  console.log("no release tag yet; the compatibility gate has nothing to compare");
}

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
  run(["git", "commit", "-m", `bump to v${next}`]);
}

console.log(`Done: ${next}`);
if (doCommit) {
  console.log(`Next: bun scripts/release.ts   # pushes main; CI checks, then tags v${next}, page, zig fetch`);
}
