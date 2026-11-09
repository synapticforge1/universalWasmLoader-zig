// Tests for scripts/github-release.ts against a mock of GitHub's REST API
// (`bun test scripts/`). The mock records every request, so each case can
// assert that a refusal made NO write.
import { afterAll, beforeAll, expect, test } from "bun:test";
import { join } from "node:path";

const SCRIPT = join(import.meta.dir, "github-release.ts");
const REPO = "owner/repo";
const TAG = "v9.9.9";
const SHA = "a".repeat(40);
const OTHER = "b".repeat(40);
const TAGOBJ = "c".repeat(40);

type State = {
  tag: null | { type: "commit" | "tag"; sha: string }; // the ref's object
  tagObject?: { type: string; sha: string }; // what an annotated tag object points at
  release: null | Record<string, unknown>;
  failReleaseGet?: number; // HTTP status for GET /releases/tags/…
  postStatus?: number; // HTTP status for POST /releases
  postMakesDraft?: boolean;
  moveTagOnPost?: string; // the ref's commit after the POST
  stallMs?: number; // delay every response
};

let state: State;
let requests: { method: string; path: string; body: any }[] = [];
let server: ReturnType<typeof Bun.serve>;

beforeAll(() => {
  server = Bun.serve({
    port: 0,
    async fetch(req) {
      const url = new URL(req.url);
      const body = req.method === "POST" ? await req.json() : null;
      requests.push({ method: req.method, path: url.pathname, body });
      if (state.stallMs) await Bun.sleep(state.stallMs);
      const json = (status: number, v: unknown) => new Response(JSON.stringify(v), { status });
      if (url.pathname === `/repos/${REPO}/git/ref/tags/${TAG}`) {
        return state.tag ? json(200, { object: state.tag }) : json(404, { message: "Not Found" });
      }
      if (url.pathname === `/repos/${REPO}/git/tags/${TAGOBJ}` && state.tagObject) {
        return json(200, { object: state.tagObject });
      }
      if (url.pathname === `/repos/${REPO}/releases/tags/${TAG}`) {
        if (state.failReleaseGet) return json(state.failReleaseGet, { message: "boom" });
        return state.release ? json(200, state.release) : json(404, { message: "Not Found" });
      }
      if (req.method === "POST" && url.pathname === `/repos/${REPO}/releases`) {
        const status = state.postStatus ?? 201;
        if (status !== 201) return json(status, { message: "Validation Failed" });
        state.release = { tag_name: body.tag_name, draft: state.postMakesDraft ?? false, html_url: "https://example/r", author: { login: "github-actions[bot]" } };
        if (state.moveTagOnPost) state.tag = { type: "commit", sha: state.moveTagOnPost };
        return json(201, state.release);
      }
      return json(404, { message: `unmocked ${req.method} ${url.pathname}` });
    },
  });
});

afterAll(() => server.stop(true));

async function run(env: Record<string, string> = {}, args: string[] = []) {
  requests = [];
  const p = Bun.spawn(["bun", SCRIPT, ...args], {
    env: {
      ...process.env,
      GITHUB_REPOSITORY: REPO,
      RELEASE_TAG: TAG,
      RELEASE_SHA: SHA,
      GITHUB_TOKEN: "t",
      GITHUB_API_URL: `http://127.0.0.1:${server.port}`,
      RELEASE_TIMEOUT_MS: "2000",
      ...env,
    },
    stdout: "pipe",
    stderr: "pipe",
  });
  const [out, err, code] = await Promise.all([new Response(p.stdout).text(), new Response(p.stderr).text(), p.exited]);
  return { code, out: out + err };
}

const posts = () => requests.filter((r) => r.method === "POST");

test("a missing tag is refused, and nothing is written (POST would create it)", async () => {
  state = { tag: null, release: null };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("does not exist on GitHub");
  expect(posts()).toHaveLength(0);
});

test("a lightweight tag at the commit: the Release is created with generated notes", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null };
  const r = await run();
  expect(r.code).toBe(0);
  expect(posts()).toHaveLength(1);
  expect(posts()[0].body).toEqual({ tag_name: TAG, name: TAG, generate_release_notes: true, draft: false, prerelease: false });
  expect(r.out).toContain("created by github-actions[bot]");
});

test("an annotated tag is peeled to its commit", async () => {
  state = { tag: { type: "tag", sha: TAGOBJ }, tagObject: { type: "commit", sha: SHA }, release: null };
  const r = await run();
  expect(r.code).toBe(0);
  expect(posts()).toHaveLength(1);
});

test("a tag at another commit is refused, no write", async () => {
  state = { tag: { type: "commit", sha: OTHER }, release: null };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain(`is ${OTHER}, the checks passed ${SHA}`);
  expect(posts()).toHaveLength(0);
});

test("an annotated tag peeling to another commit is refused, no write", async () => {
  state = { tag: { type: "tag", sha: TAGOBJ }, tagObject: { type: "commit", sha: OTHER }, release: null };
  expect((await run()).code).toBe(1);
  expect(posts()).toHaveLength(0);
});

test("a Release already there is done: exit 0, no write (re-runs are safe)", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: { tag_name: TAG, html_url: "https://example/r" } };
  const r = await run();
  expect(r.code).toBe(0);
  expect(r.out).toContain("already exists");
  expect(posts()).toHaveLength(0);
});

test("an unexpected status reading the Release is refused, no write", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null, failReleaseGet: 500 };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("HTTP 500");
  expect(posts()).toHaveLength(0);
});

test("a refused POST fails the job", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null, postStatus: 422 };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("POST release v9.9.9: HTTP 422");
});

test("a read-back that is not a published Release fails the job", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null, postMakesDraft: true };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("read-back");
});

test("a tag that moves while the Release is written fails the job", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null, moveTagOnPost: OTHER };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("moved");
});

test("a server that does not answer hits the time limit, no write", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null, stallMs: 3000 };
  const r = await run({ RELEASE_TIMEOUT_MS: "300" });
  expect(r.code).toBe(1);
  expect(r.out).toContain("timed out after 300 ms");
  expect(posts()).toHaveLength(0);
});

test("no token: every check runs, the write is refused", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null };
  const r = await run({ GITHUB_TOKEN: "" });
  expect(r.code).toBe(1);
  expect(r.out).toContain("GITHUB_TOKEN is not set");
  expect(posts()).toHaveLength(0);
});

test("--dry-run reads everything and writes nothing", async () => {
  state = { tag: { type: "commit", sha: SHA }, release: null };
  const r = await run({}, ["--dry-run"]);
  expect(r.code).toBe(0);
  expect(r.out).toContain("dry-run: would POST");
  expect(posts()).toHaveLength(0);
});
