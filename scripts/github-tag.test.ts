// Tests for scripts/github-tag.ts against a mock of GitHub's REST API (`bun test scripts/`).
// The mock records every request, so each refusal is shown to write nothing.
import { afterAll, beforeAll, expect, test } from "bun:test";
import { join } from "node:path";

const SCRIPT = join(import.meta.dir, "github-tag.ts");
const REPO = "owner/repo";
const TAG = "v9.9.9";
const SHA = "a".repeat(40);
const OTHER = "b".repeat(40);
const TAGOBJ = "c".repeat(40);

type State = {
  ref: null | { type: "commit" | "tag"; sha: string }; // refs/tags/TAG's object
  tagObjects: Record<string, { type: string; sha: string }>; // annotated tag objects by sha
  tagPostStatus?: number;
  refPostStatus?: number;
  refPostRace?: string; // on POST git/refs: answer 422 and make the ref point at this tag object
  refAfterPost?: { type: "commit" | "tag"; sha: string }; // what the ref reads back as
  stallMs?: number;
  refGetStatus?: number; // HTTP status for GET refs/tags/TAG, other than 200 / 404
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
      if (req.method === "GET" && url.pathname === `/repos/${REPO}/git/ref/tags/${TAG}`) {
        if (state.refGetStatus) return json(state.refGetStatus, { message: "boom" });
        return state.ref ? json(200, { object: state.ref }) : json(404, { message: "Not Found" });
      }
      const tagObj = url.pathname.match(new RegExp(`^/repos/${REPO}/git/tags/([0-9a-f]{40})$`));
      if (req.method === "GET" && tagObj) {
        const o = state.tagObjects[tagObj[1]];
        return o ? json(200, { object: o }) : json(404, { message: "Not Found" });
      }
      if (req.method === "POST" && url.pathname === `/repos/${REPO}/git/tags`) {
        if (state.tagPostStatus && state.tagPostStatus !== 201) return json(state.tagPostStatus, { message: "nope" });
        state.tagObjects[TAGOBJ] = { type: body.type, sha: body.object };
        return json(201, { sha: TAGOBJ, tag: body.tag });
      }
      if (req.method === "POST" && url.pathname === `/repos/${REPO}/git/refs`) {
        if (state.refPostRace) {
          state.ref = { type: "tag", sha: state.refPostRace };
          return json(422, { message: "Reference already exists" });
        }
        if (state.refPostStatus && state.refPostStatus !== 201) return json(state.refPostStatus, { message: "nope" });
        state.ref = state.refAfterPost ?? { type: "tag", sha: body.sha };
        return json(201, { ref: body.ref, object: state.ref });
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

test("no tag yet: an annotated tag object on the commit, then its ref, then a read-back", async () => {
  state = { ref: null, tagObjects: {} };
  const r = await run();
  expect(r.code).toBe(0);
  expect(posts().map((p) => p.path)).toEqual([`/repos/${REPO}/git/tags`, `/repos/${REPO}/git/refs`]);
  expect(posts()[0].body).toEqual({ tag: TAG, message: `universalWasmLoader-zig ${TAG}`, object: SHA, type: "commit" });
  expect(posts()[1].body).toEqual({ ref: `refs/tags/${TAG}`, sha: TAGOBJ });
  expect(r.out).toContain(`tag ${TAG} created on ${SHA}`);
});

test("the tag already there at this commit (annotated): done, no write (re-run safe)", async () => {
  state = { ref: { type: "tag", sha: TAGOBJ }, tagObjects: { [TAGOBJ]: { type: "commit", sha: SHA } } };
  const r = await run();
  expect(r.code).toBe(0);
  expect(r.out).toContain("already exists");
  expect(posts()).toHaveLength(0);
});

test("the tag already there at this commit (lightweight): done, no write", async () => {
  state = { ref: { type: "commit", sha: SHA }, tagObjects: {} };
  expect((await run()).code).toBe(0);
  expect(posts()).toHaveLength(0);
});

test("the tag already there at ANOTHER commit is refused, never moved", async () => {
  state = { ref: { type: "tag", sha: TAGOBJ }, tagObjects: { [TAGOBJ]: { type: "commit", sha: OTHER } } };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("never moves");
  expect(posts()).toHaveLength(0);
});

test("an unexpected status reading the ref is refused, no write (500 is not 'no tag')", async () => {
  state = { ref: null, tagObjects: {}, refGetStatus: 500 };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("GET tag ref v9.9.9: HTTP 500");
  expect(posts()).toHaveLength(0);
});

test("a refused tag-object POST fails the job, and no ref is written", async () => {
  state = { ref: null, tagObjects: {}, tagPostStatus: 403 };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("POST git/tags v9.9.9: HTTP 403");
  expect(posts().filter((p) => p.path.endsWith("/git/refs"))).toHaveLength(0);
});

test("a refused ref POST fails the job", async () => {
  state = { ref: null, tagObjects: {}, refPostStatus: 403 };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("POST git/refs tags/v9.9.9: HTTP 403");
});

test("a concurrent run made the same tag on the same commit (422): accepted after the read-back", async () => {
  const OTHERTAGOBJ = "d".repeat(40);
  state = { ref: null, tagObjects: { [OTHERTAGOBJ]: { type: "commit", sha: SHA } }, refPostRace: OTHERTAGOBJ };
  const r = await run();
  expect(r.code).toBe(0);
  expect(r.out).toContain("appeared meanwhile");
});

test("a concurrent run made the tag on ANOTHER commit (422): refused", async () => {
  const OTHERTAGOBJ = "d".repeat(40);
  state = { ref: null, tagObjects: { [OTHERTAGOBJ]: { type: "commit", sha: OTHER } }, refPostRace: OTHERTAGOBJ };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain(`peels to ${OTHER}`);
});

test("a read-back that peels elsewhere fails the job", async () => {
  state = { ref: null, tagObjects: {}, refAfterPost: { type: "commit", sha: OTHER } };
  const r = await run();
  expect(r.code).toBe(1);
  expect(r.out).toContain("read-back");
});

test("a server that does not answer hits the time limit, no write", async () => {
  state = { ref: null, tagObjects: {}, stallMs: 3000 };
  const r = await run({ RELEASE_TIMEOUT_MS: "300" });
  expect(r.code).toBe(1);
  expect(r.out).toContain("timed out after 300 ms");
  expect(posts()).toHaveLength(0);
});

test("no token: every check runs, the write is refused", async () => {
  state = { ref: null, tagObjects: {} };
  const r = await run({ GITHUB_TOKEN: "" });
  expect(r.code).toBe(1);
  expect(r.out).toContain("GITHUB_TOKEN is not set");
  expect(posts()).toHaveLength(0);
});

test("--dry-run reads and writes nothing", async () => {
  state = { ref: null, tagObjects: {} };
  const r = await run({}, ["--dry-run"]);
  expect(r.code).toBe(0);
  expect(r.out).toContain("dry-run: would create");
  expect(posts()).toHaveLength(0);
});

test("a tag that is not vX.Y.Z is refused before any request", async () => {
  state = { ref: null, tagObjects: {} };
  const r = await run({ RELEASE_TAG: "latest" });
  expect(r.code).toBe(1);
  expect(requests).toHaveLength(0);
});
