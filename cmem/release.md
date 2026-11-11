# release.md — how universalWasmLoader-zig releases, and every release so far

🔒 The ONE place for release records (owner, 2026-11-10, workspace letter H33 D3:
"releases should be recorded in one place in every project; … release.md should be the file name
with the newest first in the 'Release History' with a summary of what changed in each one").
The records that lived in [overview.md](overview.md) moved here on 2026-11-11; it keeps a pointer.

## The release process (workspace `release-process.md` § 5, U1–U12; since 2026-11-11)

🔒 Owner, 2026-11-10 (H33 D1): **"the CI to check first, then push the tag, and then create the
release page from the tag, then publish the package to the registry for the project in that
order."** The owner's words (H34, [working-rules.md](working-rules.md)): "commit" ⊂ "push" ⊂
"release"; "release" = "push", then this process.

This is a **git-tag ecosystem** (`release-process.md` § 6): the Zig package manager and Zigistry
resolve `https://github.com/synapticforge1/universalWasmLoader-zig/archive/refs/tags/vX.Y.Z.tar.gz`.
There is no registry upload — **CI's tag IS the publish**, and U8 is the read-back as a user.

| slot | here |
| --- | --- |
| **U1** go + level | The owner's word and an explicit `patch` / `minor` / `major`; `bump-version.ts` refuses a missing level (no default). Ask if the level was not given (H34). |
| **U2** merge first | Work is merged into `main` ("push"); the bump is its own commit, made by the tool: `bump to vX.Y.Z` (since 2026-11-11; `Bump version: vA -> vB` before). |
| **U3** bump tool | `scripts/bump-version.ts`: clean tree (untracked files count), the version line equals the last tag, the target tag absent, `api-surface.txt` fresh, the floor from the surface diff + SPEC version ([compat-gate.md](compat-gate.md)) refuses a lower level, and for a patch / minor the previous tag's tests run green on this tree. The one version line is `build.zig.zon` `.version`. |
| **U4** release script | `scripts/release.ts`: guards — clean tree (untracked count), on `main`, `origin/main` (fetched) an ancestor of HEAD, `vX.Y.Z` absent locally AND on origin (`ls-remote`); a cold-cache `zig build run` + `zig build test`; `compat.ts --release`; then **`git push origin main` only** and the `ls-remote` read-back of `main`. **It makes and pushes NO tag** (since 2026-11-11). Never `git tag -f`, never a forced tag push. |
| **U5** `checks` | `.github/workflows/release.yml`, on every push to `main`, windows-latest (`build.zig` knows only the MinGW wasmtime SDK), `contents: read`, a token-less clone of `github.sha`: "is this a release?" (`v` + `.version` has no tag, or — a re-run — its tag is on THIS commit); the package hash `zig fetch .` computes (it covers `build.zig.zon` `paths` only); pinned Bun / Zig / wasmtime SDK; `zig build test` (output `All N tests passed.`), `zig build run` (the three adder lines, exact), `surface.ts --check`, `bun test scripts/`, `compat.ts --release` (report mode when not a release). |
| **U6** `tag` | Only when `checks` is green and it is a release; `contents: write`; fetches only `scripts/github-tag.ts` at `github.sha`: an existing tag must peel to the commit (then done, no write) — anywhere else is refused, a tag never moves; else the annotated tag object, then `refs/tags/vX.Y.Z`, then a read-back that peels to the commit. The tag is pushed with the job's `GITHUB_TOKEN`, which starts no new run, so U7–U8 follow in the SAME run by `needs:`. |
| **U7** `release` | `contents: write`; fetches only `scripts/github-release.ts`: H32's order — the tag peels to the commit, an existing page is done, `POST` with an install line on top (`zig fetch --save <tarball>`) and `generate_release_notes`, read back (not a draft), peel again. 30 s per request. |
| **U8** `publish` | `permissions: {}`. The tag is the publish; the job is the read-back as a user: pinned Linux Zig, `zig fetch` of the tag's tarball (5 tries, 120 s each) must give EXACTLY the hash `checks` computed from the commit — fatal otherwise. Nothing to skip: it only reads. Provenance: none exists for a git tarball; the nearest equivalent is this hash equality (the tarball is the tested commit's `paths`). |
| **U9** pins | No `uses:` at all; Bun 1.4.4 (H31), Zig 0.17.1 (Windows and Linux) and the wasmtime 45.0.2 SDK by version AND sha256 in the workflow `env`; `permissions: {}` at the top, each job its own; `concurrency` per commit, never cancelled. |
| **U10** as a user | Done IN CI by U8 (`zig fetch`); after a release, also locally: `zig fetch <tarball>` = the recorded hash. |
| **U11** record | This file, § "Release History", newest first: what changed, the run ids, the read-backs. |
| **U12** failure | Re-run failed jobs: `checks` sees its own tag on the commit (a re-run), `tag` finds the tag on the commit and writes nothing, `release` finds the page and writes nothing, `publish` only reads. Never a new tag for the same code, never a moved tag. A changed release job is rehearsed before it lands (workspace best-practices 7.22) and lands before a release, never during one. |
| **Zigistry** | Not a release step: the `zig-package` repository topic, a GitHub web-UI task — an **owner item**. `bun scripts/publish.ts` checks the tag and the topic (public REST API) and prints it. |

**Rehearsed 2026-11-11, before landing** (GitHub untouched where not stated):
- `scripts/github-tag.ts`: 14 cases on a `Bun.serve` mock that records every request (create on a
  new version; existing tag on the commit, annotated and lightweight → done, no write; existing
  elsewhere → refused; 500 on the read is not "no tag"; tag-object / ref POST refused; a
  concurrent 422 on the same commit accepted after the read-back, on another commit refused; a
  read-back elsewhere; a stalled API → `timed out after 300 ms`; no token; dry-run; a non-`vX.Y.Z`
  tag before any request). Six one-line mutants, each caught only by its own case(s). Real GitHub,
  read-only dry runs: `v2.1.1` at `d42f252` → "already exists … nothing to do"; `v2.1.1` at
  another commit → refused ("never moves"); `v9.9.9` → "would create".
- `scripts/release.ts` in a scratch clone with a scratch bare remote: refused for the
  already-tagged 2.1.1, off `main`, a diverged `origin/main`, and a `v2.1.2` that exists only on
  the remote; the real path (`bump-version.ts patch` → `release.ts`) pushed `main` and read it
  back, and afterwards **no `v2.1.2` tag existed locally or on the remote**.
- `checks`' "is this a release?" logic on this repo: a commit after the 2.1.1 release → false;
  `d42f252` (the tagged commit) → true (re-run); a new version → true. `zig fetch .` of a clean
  clone of `v2.1.1` = the published tarball's hash; the `publish` comparison passes on the real
  `v2.1.1` tarball and fails on a wrong expected hash.
- Measured on GitHub only by the next release run (the first `tag` and `publish` jobs).

## Release History

### v2.1.1 — 2026-11-09 · the first CI-made Release page

**What changed:** no library change (header = loader-c 2.1.1 = 2.1.0, surface 120 symbols, floor
patch). Added `.github/workflows/release.yml` (H32): `checks` on GitHub, then the GitHub Release
page by CI through the REST API (`scripts/github-release.ts`); Bun 1.4.4 pins (H31); the wasmtime
SDK download pinned by sha256 (`fetch-wasmtime.ts` `WASMTIME_SHA256`); `release.ts` printed the
run and page URLs instead of an owner item.

**Record** (owner: "update the project memory files, commit, push then release"): memory
`8c3bb37` pushed first; its `main` push ran `checks` on GitHub for the first time, run
**42654594017**, every step green, `release` skipped (a branch push). Then `bump-version.ts
patch` → **`d42f252`** (v2.1.0's 25 tests green), `release.ts` on a new empty cache (adder lines,
`All 25 tests passed.`, `compat --release` patch vs floor patch), annotated tag **`v2.1.1`** =
object `b1708f7` → `d42f252`; its read-back printed `main = v2.1.1^{} = d42f252`. The tag's run
**42655501506**: `checks` and `release` green. Release `v2.1.1` read through the public REST API:
**`author.login` = `github-actions[bot]`**, not a draft, generated notes; the tag unmoved. Tarball
HTTP 200; `zig fetch` → `universal_wasm_loader-2.1.1-kwfgarYFAgCdmh3zo0DyL2M5Sy5kqj4oENLGIs_e7xRG`.
(Runs read with a one-time Bun poller over `GET /repos/…/actions/runs?head_sha=…`; a branch push
and a tag push of one commit give two runs with one `head_sha`, told apart by `head_branch`.)

### v2.1.0 — 2026-11-09 · a host callback can fail

**What changed:** the vendored header became loader-c 2.1.0's (`uwl_err` / `UWL_ERR`, workspace
C39): a callback returns `uwl.c.uwl_err("why")` and, for any import (void ones included), the
WASM stops there and the export's call is `error.CallFailed` naming it; two tests for it (25).
`release.ts` / `publish.ts` stopped using the GitHub CLI (H30): `ls-remote` read-backs, web-UI
tasks printed as owner items. Additive → MINOR.

**Record** (owner: "update the project memory files, commit, bump, push and release"): memory
`1026798`; `bump-version.ts minor` → `fdffe9f` (floor minor: `added: const c.UWL_ERR`, `added: fn
c.uwl_err`; **Rule 3's first real run:** v2.0.0's own 23 tests green on the new src); `4a198dc`
fixed `bump-version.ts`'s stale "creates the GitHub Release" lines. `release.ts` on a new empty
cache: adder lines, `All 25 tests passed.`, `compat --release` minor vs floor minor. Annotated tag
**`v2.1.0`** = object `93267f5` → **`4a198dc`**, read back `main = v2.1.0^{} = 4a198dc`. `git grep
-w gh origin/main -- scripts` → nothing. Tarball HTTP 200; `zig fetch` →
`universal_wasm_loader-2.1.0-kwfgarYFAgA-W5Whn_YYPHuk_A3ZJHl-G8DT_JyzNZlr`. No Release page (no
backfill, owner H32).

### v2.0.0 — 2026-11-08 · SPEC 4.0.0 and Zig 0.17.1

**What changed:** SPEC 4.0.0's loud failures from loader-c 2.0.0 (workspace C29): only an absent
`.wit` is raw, strict `.wit` parsing, every WIT import needs a callback (and its result kind is
checked), typed-call arguments checked and never coerced — the binding sends each argument's kind
by its Zig type, and `call(T)` refuses a result of another kind (S8). New `Module.importWith`,
`uwl.lastError()`, `HostCallback`. A `uwl_str` argument leak fixed. Ported to Zig 0.17.1
(`@cImport` → `addTranslateC`, and four more removed APIs). Tests 11 → 23 (the SPEC § 8
refusals; the s64 / f32 literal limitation pinned). Breaking → MAJOR (README § Breaking Changes).

**Record** (owner: "update the project memory files, commit, bump, then release and push"; the
owner agreed 2.0.0): the first release through the compatibility gate — `bump-version.ts major`
(floor major: SPEC 3.0.0 → 4.0.0, and v1.0.0's surface is not computable on Zig 0.17.1) →
**`c647164`**; `release.ts` on a new empty cache (it only deletes the repo's own `.zig-cache`,
which an NTFS cache dir bypasses): adder lines, `All 23 tests passed.`, `compat --release` major vs
floor major. Annotated tag **`v2.0.0`** = object `932b94c` → **`c647164`**; `ls-remote`: `main` =
`v2.0.0^{}` = `c647164`. Tarball HTTP 200; `zig fetch` →
`universal_wasm_loader-2.0.0-kwfgauDsAQB_yLRu7NqzpGuKdXu9FRj8mTy91DtmyO_g`. No Release page (no
backfill, owner H32).

### v1.0.0 — 2026-06-19 · the first release

**What changed:** the idiomatic Zig binding over the C single-header loader (SPEC 3.0.0):
`Module.import`, `call(T, …)`, `callStr`, `deinit`, the raw `uwl.c` layer; Zig 0.16; the wasmtime
C API v45.0.2 import library.

**Record** (re-verified 2026-10-21 against `origin`): `v1.0.0` is an **annotated** tag — object
`89a6d97` → **commit `07d954a`** ("Add scripts/bump-version.sh"; resolve with `git rev-parse
'v1.0.0^{commit}'`). The tagged tree is the working library + LICENSE + cmem plus
`fetch-wasmtime.sh`, `bump-version.sh` and `release.sh`, not `publish.sh` or the `.nu` ports; the
tag was intentionally not moved (scripts / cmem are not package surface). No tests at the tag, so
Rule 3 printed NOTHING TO RUN for the first bump after it. No Release page (no backfill).
