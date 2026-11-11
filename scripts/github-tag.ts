/**
 * Create the release tag IN CI, through GitHub's REST API (workspace letter H33, `release-process.md`
 * § 5 U6; owner, 2026-11-10: "the CI to check first, then push the tag, and then create the release
 * page from the tag, then publish"). Run by `.github/workflows/release.yml`'s `tag` job only after
 * `checks` passed on THIS commit; the job fetches only this file at the commit (no checkout, C30).
 * Never the GitHub CLI (H30), never from the owner's machine.
 *
 *   bun github-tag.ts            # create the annotated tag on RELEASE_SHA, or confirm it is there
 *   bun github-tag.ts --dry-run  # every check and read, no write
 *
 * Environment:
 *   GITHUB_REPOSITORY  owner/repo                    (set by Actions)
 *   RELEASE_TAG        the tag, e.g. v2.1.2          (v + build.zig.zon's .version)
 *   RELEASE_SHA        the commit `checks` passed    (github.sha)
 *   GITHUB_TOKEN       contents: write — only for the write
 *   GITHUB_API_URL     default https://api.github.com (a mock in the tests)
 *   RELEASE_TIMEOUT_MS per-request time limit, default 30000 (workspace best-practices 7.21)
 *
 * Each step refuses with exit 1 on anything unexpected:
 *   1. The tag already on GitHub: it must peel to RELEASE_SHA — then done, no write (a re-run is
 *      safe). Anywhere else: refuse. A published tag never moves (U4, U12).
 *   2. Otherwise create the annotated tag object on RELEASE_SHA, then the ref `refs/tags/<tag>`.
 *   3. Read the ref back and peel it: it must be RELEASE_SHA.
 *
 * `node:` builtins and `fetch` only; self-contained, because the job runs it alone.
 */
import process from "node:process";

const args = process.argv.slice(2);
const dryRun = args.includes("--dry-run");
for (const a of args) if (a !== "--dry-run") fail(`unknown argument "${a}" — use --dry-run or nothing`);

function fail(msg: string): never {
  console.error(`github-tag: ${msg}`);
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
    const signal = AbortSignal.timeout(timeoutMs); // request to last byte
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

/** The commit the tag peels to on GitHub, or null when the tag does not exist. */
async function peeled(): Promise<string | null> {
  const ref = await call("GET", `/repos/${repo}/git/ref/tags/${tag}`);
  if (ref.status === 404) return null;
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

// 1. Already there?
const existing = await peeled();
if (existing !== null) {
  if (existing !== sha) fail(`tag ${tag} already exists at ${existing}, not ${sha}; a published tag never moves — refusing`);
  console.log(`tag ${tag} already exists at ${sha} — nothing to do`);
  process.exit(0);
}

// 2. Create the annotated tag object, then its ref.
if (dryRun) {
  console.log(`dry-run: would create annotated tag ${tag} on ${sha} (POST git/tags, POST git/refs)`);
  process.exit(0);
}
if (!token) fail("GITHUB_TOKEN is not set; cannot create the tag");
const obj = await call("POST", `/repos/${repo}/git/tags`, {
  tag,
  message: `universalWasmLoader-zig ${tag}`,
  object: sha,
  type: "commit",
});
if (obj.status !== 201 || !/^[0-9a-f]{40}$/.test(obj.json?.sha ?? "")) fail(`POST git/tags ${tag}: HTTP ${obj.status} ${obj.text.slice(0, 200)}`);
const made = await call("POST", `/repos/${repo}/git/refs`, { ref: `refs/tags/${tag}`, sha: obj.json.sha });
if (made.status !== 201) {
  // 422 = the ref appeared meanwhile (a concurrent run): fine only if it peels to this commit.
  if (made.status !== 422) fail(`POST git/refs tags/${tag}: HTTP ${made.status} ${made.text.slice(0, 200)}`);
  console.log(`refs/tags/${tag} appeared meanwhile (HTTP 422); checking where it points`);
}

// 3. Read it back.
const after = await peeled();
if (after !== sha) fail(`read-back: tag ${tag} peels to ${after ?? "(missing)"}, not ${sha}`);
console.log(`tag ${tag} created on ${sha} (tag object ${obj.json.sha})`);
