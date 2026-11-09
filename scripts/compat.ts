// The compatibility gate (workspace layout-and-packaging.md § "The compatibility
// gate", Rules 2 and 3): the FLOOR a release must meet, and the previous tag's
// tests run against the current source.
//
// Usage: bun scripts/compat.ts             report: last tag, floor, previous-tag tests
//        bun scripts/compat.ts --release   at a release commit: .version must meet the
//                                          floor vs the tag before it, and the previous
//                                          tests must pass for a patch/minor
//        add --no-tests to skip the previous-tag tests (floor only)
//
// A previous tag's source and tests are extracted with `git archive` into .compat/
// (gitignored, deleted after) and built through build.zig's -Dsrc / -Dtest-root /
// -Dtest-cwd options. The test verdict is read from the suite's OUTPUT as well as
// its exit code.
import { mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { die, help, readVersion, ROOT } from "./_lib.ts";
import { diffSurface, generate, parse, readSnapshot, SNAPSHOT, type Surface } from "./surface.ts";

export type Level = "patch" | "minor" | "major";
export const LEVELS: Level[] = ["patch", "minor", "major"];
export const rank = (l: Level) => LEVELS.indexOf(l);
const maxLevel = (a: Level, b: Level): Level => (rank(a) >= rank(b) ? a : b);

export function parseVersion(v: string): [number, number, number] {
  const m = v.match(/^v?(\d+)\.(\d+)\.(\d+)$/);
  if (!m) throw new Error(`not a MAJOR.MINOR.PATCH version: ${v}`);
  return [Number(m[1]), Number(m[2]), Number(m[3])];
}

export function compareVersions(a: string, b: string): number {
  const [x, y] = [parseVersion(a), parseVersion(b)];
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
}

/** The level of the step from `from` to `to` (to must be greater). */
export function levelOf(from: string, to: string): Level {
  const [a, b] = [parseVersion(from), parseVersion(to)];
  if (compareVersions(from, to) >= 0) throw new Error(`${to} is not above ${from}`);
  if (b[0] !== a[0]) return "major";
  if (b[1] !== a[1]) return "minor";
  return "patch";
}

// Rule 2: removed or changed -> major; added -> minor; nothing -> patch.
// The SPEC version moves the floor by its own level; lowering it is major.
// A changed signature is major even when it only widens: the gate cannot tell
// widening from narrowing, so it errs high.
export function floorOf(prev: Surface, next: Surface): { floor: Level; reasons: string[] } {
  let floor: Level = "patch";
  const reasons: string[] = [];
  for (const line of diffSurface(prev, next)) {
    if (line.startsWith("spec: ")) continue;
    const l: Level = line.startsWith("added: ") ? "minor" : "major";
    floor = maxLevel(floor, l);
    reasons.push(`${l}: ${line}`);
  }
  if (prev.spec !== next.spec) {
    const lowered = compareVersions(next.spec, prev.spec) < 0;
    const l: Level = lowered ? "major" : levelOf(prev.spec, next.spec);
    floor = maxLevel(floor, l);
    reasons.push(`${l}: SPEC ${prev.spec} -> ${next.spec}${lowered ? " (lowered)" : ""}`);
  }
  return { floor, reasons };
}

export function git(args: string[]): string {
  const p = Bun.spawnSync(["git", ...args], { cwd: ROOT, stdout: "pipe", stderr: "pipe" });
  if (p.exitCode !== 0) throw new Error(`git ${args.join(" ")} failed: ${p.stderr.toString().trim()}`);
  return p.stdout.toString();
}

/** Release tags reachable from HEAD, highest first. */
export function releaseTags(): string[] {
  return git(["tag", "--merged", "HEAD", "--list", "v*"])
    .split("\n")
    .map((t) => t.trim())
    .filter((t) => /^v\d+\.\d+\.\d+$/.test(t))
    .sort((a, b) => compareVersions(b, a));
}

const COMPAT = join(ROOT, ".compat");

/** Extract `paths` of `tag` into .compat/ (relative path + cwd: GNU tar reads `<drive>:\…` as host:path). */
function extract(tag: string, paths: string[]): void {
  rmSync(COMPAT, { recursive: true, force: true });
  mkdirSync(COMPAT, { recursive: true });
  const tar = Bun.spawnSync(["git", "archive", "--format=tar", "-o", ".compat/tag.tar", tag, ...paths], { cwd: ROOT, stderr: "pipe" });
  if (tar.exitCode !== 0) throw new Error(`git archive ${tag} ${paths.join(" ")} failed: ${tar.stderr.toString().trim()}`);
  const x = Bun.spawnSync(["tar", "-xf", "tag.tar"], { cwd: COMPAT, stderr: "pipe" });
  if (x.exitCode !== 0) throw new Error(`tar -xf failed: ${x.stderr.toString().trim()}`);
}

/**
 * The surface of `tag`: its committed api-surface.txt when it has one (the
 * release gate refuses a stale snapshot, so at a tag it IS that tag's surface,
 * whatever Zig built it); otherwise its src/ built with today's tools/surface.zig.
 */
export function surfaceAt(tag: string): Surface {
  if (git(["ls-tree", "--name-only", tag, SNAPSHOT]).trim() !== "") {
    return parse(git(["show", `${tag}:${SNAPSHOT}`]));
  }
  try {
    extract(tag, ["src"]);
    return generate(".compat/src", git(["show", `${tag}:src/universal_wasm_loader.h`]));
  } finally {
    rmSync(COMPAT, { recursive: true, force: true });
  }
}

/**
 * Rule 2 against `tag`. A tag with no snapshot whose src/ does not build on
 * today's Zig (v1.0.0 uses `@cImport`, removed in Zig 0.17) has no computable
 * surface: the floor is then MAJOR, erring high as the rest of the gate does.
 */
export function floorVs(tag: string, next: Surface): { floor: Level; reasons: string[] } {
  let prev: Surface;
  try {
    prev = surfaceAt(tag);
  } catch (e) {
    const first = String(e instanceof Error ? e.message : e).split("\n")[0];
    return {
      floor: "major",
      reasons: [`major: ${tag}'s surface is not computable (no ${SNAPSHOT} at the tag, and its src/ does not build on this Zig: ${first})`],
    };
  }
  return floorOf(prev, next);
}

export type TestResult = { ok: boolean; none: boolean; passed: number; failed: number; exitCode: number; output: string };

/** Rule 3: the tag's tests (suite AND fixtures, unmodified) against the current source. */
export function previousTests(tag: string, keep = false): TestResult {
  const hasSuite = git(["ls-tree", "--name-only", tag, "tests/uwl_test.zig"]).trim() !== "";
  if (!hasSuite) return { ok: true, none: true, passed: 0, failed: 0, exitCode: 0, output: "" };
  try {
    extract(tag, ["tests"]);
    const run = Bun.spawnSync(
      ["zig", "build", "test", "-Dtest-root=.compat/tests/uwl_test.zig", "-Dtest-cwd=.compat"],
      { cwd: ROOT, stdout: "pipe", stderr: "pipe" },
    );
    const output = run.stdout.toString() + run.stderr.toString();
    const all = output.match(/All (\d+) tests passed\./);
    const some = output.match(/(\d+) passed; \d+ skipped; (\d+) failed\./);
    const passed = all ? Number(all[1]) : some ? Number(some[1]) : 0;
    const failed = all ? 0 : some ? Number(some[2]) : 0;
    const exitCode = run.exitCode ?? 1;
    const counted = all !== null || some !== null;
    return { ok: exitCode === 0 && counted && failed === 0 && passed > 0, none: false, passed, failed, exitCode, output };
  } finally {
    if (!keep) rmSync(COMPAT, { recursive: true, force: true });
  }
}

export function describeTests(tag: string, r: TestResult): string {
  if (r.none) return `previous-tag tests: ${tag} has no tests/uwl_test.zig — NOTHING TO RUN (Rule 3 unverified for this step)`;
  const verdict = r.ok ? "GREEN" : "RED";
  return `previous-tag tests (${tag} tests/ vs current src): ${verdict} — ${r.passed} passed, ${r.failed} failed, exit ${r.exitCode}`;
}

export function printFloor(prevTag: string, f: { floor: Level; reasons: string[] }): void {
  console.log(`floor vs ${prevTag}: ${f.floor}`);
  for (const r of f.reasons) console.log(`  ${r}`);
}

/** The committed snapshot must equal the module's surface before any floor is computed. */
export function currentSurface(): Surface {
  const snapshot = readSnapshot();
  const stale = diffSurface(snapshot, generate());
  if (stale.length > 0) {
    for (const l of stale) console.error(`  ${l}`);
    die("api-surface.txt is stale; re-baseline in its own commit first (bun scripts/surface.ts)");
  }
  return snapshot;
}

const USAGE = `
Report or enforce the compatibility gate (floor + previous-tag tests).

Usage: bun scripts/compat.ts [--release] [--no-tests] [--keep]
  --release   Gate the version in build.zig.zon against the tag before it.
  --no-tests  Skip the previous tag's tests (floor only).
  --keep      Keep .compat/ after the previous-tag test run.
  -h, --help  Show this help.
`;

if (import.meta.main) {
  const args = process.argv.slice(2);
  if (args.includes("-h") || args.includes("--help")) help(USAGE);
  for (const a of args) if (!["--release", "--no-tests", "--keep"].includes(a)) die(`unknown argument: ${a}`, 2);
  process.chdir(ROOT);
  const noTests = args.includes("--no-tests");
  const keep = args.includes("--keep");
  const snapshot = currentSurface();

  if (args.includes("--release")) {
    const version = readVersion();
    const prevTag = releaseTags().find((t) => compareVersions(t, version) < 0);
    if (!prevTag) {
      console.log(`no release before ${version}; nothing to compare`);
      process.exit(0);
    }
    const step = levelOf(prevTag, version);
    const f = floorVs(prevTag, snapshot);
    console.log(`release ${version} is a ${step} step from ${prevTag}`);
    printFloor(prevTag, f);
    let ok = rank(step) >= rank(f.floor);
    if (!ok) console.error(`BLOCKED: ${step} is below the floor (${f.floor})`);
    if (step !== "major" && !noTests) {
      const r = previousTests(prevTag, keep);
      console.log(describeTests(prevTag, r));
      if (!r.ok) {
        console.error(r.output);
        console.error(`BLOCKED: a ${step} release requires the previous tag's tests to pass`);
        ok = false;
      }
    }
    process.exit(ok ? 0 : 1);
  }

  const prevTag = releaseTags()[0];
  if (!prevTag) {
    console.log("no release tag yet; nothing to compare");
    process.exit(0);
  }
  console.log(`version line: ${readVersion()}; last release tag: ${prevTag}`);
  printFloor(prevTag, floorVs(prevTag, snapshot));
  if (!noTests) {
    const r = previousTests(prevTag, keep);
    console.log(describeTests(prevTag, r));
    if (!r.ok) {
      console.error(r.output);
      process.exit(1);
    }
  }
}
