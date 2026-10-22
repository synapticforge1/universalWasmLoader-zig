// Shared helpers for the scripts in this directory (run with Bun; not a script itself).
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

/** Repo root (this file lives in scripts/). Every script chdirs here first. */
export const ROOT = resolve(import.meta.dir, "..");

export function die(msg: string, code = 1): never {
  console.error(`error: ${msg}`);
  process.exit(code);
}

/** Run a command with inherited stdio; a failure exits with the child's code (like `set -e`). */
export function exec(cmd: string[]): void {
  let code: number | null;
  try {
    code = Bun.spawnSync({ cmd, stdio: ["inherit", "inherit", "inherit"] }).exitCode;
  } catch {
    die(`cannot run '${cmd[0]}' — is it installed and on PATH?`);
  }
  if (code !== 0) process.exit(code ?? 1);
}

/** Run a command and capture its output; never exits. */
export function capture(cmd: string[]): { ok: boolean; out: string } {
  try {
    const r = Bun.spawnSync({ cmd, stdout: "pipe", stderr: "pipe" });
    return { ok: r.exitCode === 0, out: r.stdout.toString() };
  } catch {
    return { ok: false, out: "" };
  }
}

/** Echo `+ cmd` and execute it, or only echo under --dry-run. */
export function runner(dryRun: boolean): (cmd: string[]) => void {
  return (cmd) => {
    console.log(`+ ${cmd.join(" ")}`);
    if (!dryRun) exec(cmd);
  };
}

export const has = (tool: string): boolean => Bun.which(tool) !== null;

// --- build.zig.zon: the single source of truth for the version --------------
export const ZON = "build.zig.zon";
export const VERSION_RE = /(\.version\s*=\s*")([^"]+)(")/;

export function readVersion(): string {
  const m = readFileSync(ZON, "utf8").match(VERSION_RE);
  if (!m) die(`could not read .version from ${ZON}`);
  return m[2];
}

// --- git --------------------------------------------------------------------
export const statusShort = (): string => capture(["git", "status", "--porcelain"]).out.trim();

export function reportDirty(): void {
  console.error(capture(["git", "status", "--short"]).out.trimEnd());
}

export const tagExists = (tag: string): boolean =>
  capture(["git", "rev-parse", "-q", "--verify", `refs/tags/${tag}`]).ok;

export const revParse = (rev: string): string => capture(["git", "rev-parse", rev]).out.trim();

/** Print usage and exit 0. */
export function help(usage: string): never {
  console.log(usage.trim());
  process.exit(0);
}
