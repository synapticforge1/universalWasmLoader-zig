/**
 * Create the GitHub Release page for a tag through GitHub's REST API — never the GitHub CLI
 * (workspace H30) and never by hand (workspace H32; owner, 2026-11-09: "they both need the CI and
 * release pages, not by hand"). Run by `.github/workflows/release.yml`'s `release` job, which
 * fetches only this file at the tag's commit (no package code beside the write token, C30):
 *
 *   bun github-release.ts            # create the Release if it is not there
 *   bun github-release.ts --dry-run  # every check and read, no write
 *
 * Environment:
 *   GITHUB_REPOSITORY  owner/repo                    (set by Actions)
 *   RELEASE_TAG        the tag, e.g. v2.1.1
 *   RELEASE_SHA        the commit the `checks` job passed (github.sha)
 *   GITHUB_TOKEN       contents: write — only for the write; this public repo reads without it
 *   GITHUB_API_URL     default https://api.github.com (a mock in the tests)
 *   RELEASE_TIMEOUT_MS per-request time limit, default 30000 (workspace best-practices 7.21)
 *
 * The order (workspace H32 — keep it), each step refusing with exit 1 on anything unexpected:
 *   1. The tag exists on GitHub and peels (annotated tags followed) to RELEASE_SHA. `POST
 *      /releases` CREATES a missing tag, so a release is never written without this.
 *   2. A Release for the tag already there = done, no write (a re-run is safe).
 *   3. POST it with `generate_release_notes`, then read it back and re-check the tag.
 *
 * `node:` builtins and `fetch` only; self-contained, because the job runs it alone.
 */
import process from "node:process";

const args = process.argv.slice(2);
const dryRun = args.includes("--dry-run");
for (const a of args) if (a !== "--dry-run") fail(`unknown argument "${a}" — use --dry-run or nothing`);

function fail(msg: string): never {
  console.error(`github-release: ${msg}`);
  process.exit(1);
}

function need(name: string): string {
  const v = process.env[name];
  if (!v) fail(`${name} is not set`);
  return v;
}

const repo = need("GITHUB_REPOSITORY");
const tag = need("RELEASE_TAG");
const sha = need("RELEASE_SHA");
const token = process.env.GITHUB_TOKEN ?? "";
const api = (process.env.GITHUB_API_URL || "https://api.github.com").replace(/\/+$/, "");
const timeoutMs = Number(process.env.RELEASE_TIMEOUT_MS ?? "30000");

if (!/^[\w.-]+\/[\w.-]+$/.test(repo)) fail(`GITHUB_REPOSITORY "${repo}" is not owner/repo`);
if (!/^v\d+\.\d+\.\d+$/.test(tag)) fail(`RELEASE_TAG "${tag}" is not a vX.Y.Z tag`);
if (!/^[0-9a-f]{40}$/.test(sha)) fail(`RELEASE_SHA "${sha}" is not a full commit id`);
if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) fail(`RELEASE_TIMEOUT_MS "${process.env.RELEASE_TIMEOUT_MS}" is not a positive number`);

type Reply = { status: number; json: any; text: string };

async function call(method: string, path: string, body?: unknown): Promise<Reply> {
  const headers: Record<string, string> = {
    Accept: "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
    "User-Agent": `${repo} release workflow`,
  };
  if (token) headers.Authorization = `Bearer ${token}`;
  if (body !== undefined) headers["Content-Type"] = "application/json";
  let res: Response;
  let text: string;
  try {
    // One limit for the whole exchange, request to last byte.
    const signal = AbortSignal.timeout(timeoutMs);
    res = await fetch(`${api}${path}`, { method, headers, body: body === undefined ? undefined : JSON.stringify(body), signal });
    text = await res.text();
  } catch (e) {
    const err = e as Error;
    const why = err.name === "TimeoutError" || err.name === "AbortError" ? `timed out after ${timeoutMs} ms` : err.message;
    fail(`${method} ${path}: ${why}`);
  }
  let json: any = null;
  try {
    json = text ? JSON.parse(text) : null;
  } catch {
    json = null;
  }
  return { status: res.status, json, text };
}

/** The commit the tag peels to on GitHub, or a refusal. */
async function peeledTag(): Promise<string> {
  const ref = await call("GET", `/repos/${repo}/git/ref/tags/${tag}`);
  if (ref.status === 404) fail(`tag ${tag} does not exist on GitHub; refusing (a release would create it)`);
  if (ref.status !== 200) fail(`GET tag ref ${tag}: HTTP ${ref.status} ${ref.text.slice(0, 200)}`);
  let object = ref.json?.object as { type: string; sha: string } | undefined;
  for (let depth = 0; object?.type === "tag"; depth++) {
    if (depth > 5) fail(`tag ${tag} nests more than 5 tag objects`);
    const t = await call("GET", `/repos/${repo}/git/tags/${object.sha}`);
    if (t.status !== 200) fail(`GET tag object ${object.sha}: HTTP ${t.status}`);
    object = t.json?.object;
  }
  if (object?.type !== "commit") fail(`tag ${tag} does not point to a commit (${JSON.stringify(object)})`);
  return object.sha;
}

// 1. The tag exists and peels to the commit `checks` passed.
const before = await peeledTag();
if (before !== sha) fail(`tag ${tag} is ${before}, the checks passed ${sha}; refusing`);
console.log(`tag ${tag} = ${sha}`);

// 2. A Release already there is done.
const existing = await call("GET", `/repos/${repo}/releases/tags/${tag}`);
if (existing.status === 200) {
  console.log(`GitHub Release ${tag} already exists: ${existing.json?.html_url} — nothing to do`);
  process.exit(0);
}
if (existing.status !== 404) fail(`GET release ${tag}: HTTP ${existing.status} ${existing.text.slice(0, 200)}`);

// 3. Create it, read it back, re-check the tag.
// The "registry" line on top (release-process.md § 5 U7): for Zig, the tag's tarball is the
// package; GitHub appends the generated notes after this body.
const install = `Install: \`zig fetch --save https://github.com/${repo}/archive/refs/tags/${tag}.tar.gz\``;
const body = { tag_name: tag, name: tag, body: install, generate_release_notes: true, draft: false, prerelease: false };
if (dryRun) {
  console.log(`dry-run: would POST /repos/${repo}/releases (${Object.keys(body).join(", ")})`);
  process.exit(0);
}
if (!token) fail("GITHUB_TOKEN is not set; cannot write the release");
const wrote = await call("POST", `/repos/${repo}/releases`, body);
if (wrote.status !== 201) fail(`POST release ${tag}: HTTP ${wrote.status} ${wrote.text.slice(0, 200)}`);
const back = await call("GET", `/repos/${repo}/releases/tags/${tag}`);
if (back.status !== 200 || back.json?.tag_name !== tag || back.json?.draft !== false) {
  fail(`read-back of release ${tag}: HTTP ${back.status}, tag_name ${back.json?.tag_name}, draft ${back.json?.draft}`);
}
const after = await peeledTag();
if (after !== sha) fail(`tag ${tag} moved to ${after} while the release was written (checks passed ${sha})`);
console.log(`GitHub Release ${tag} created by ${back.json?.author?.login ?? "?"}: ${back.json?.html_url}`);
