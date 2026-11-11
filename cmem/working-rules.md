# working-rules.md — repo-local rules for universalWasmLoader-zig

The workspace floor (`../../../cmem/working-rules.md`) applies here in full. This file holds only
what is specific to this repo. **A repo's own rules govern inside that repo.** Scripting is in
[INDEX.md](INDEX.md) § "Scripting rule" (Bun + TypeScript only; Zig has no scripting mechanism that
does not build an executable — workspace Tooling rule 1, letter H15).

## No secrets, no personal information (workspace § Records; letters H22, H25 §3(d), H28)

- 🔒 **Nothing committed is private** (owner, 2026-10-22). Committed `cmem/` is fine as long as it
  displays no secrets and no personal information. Never in a committed file, commit message, tag,
  issue or release: tokens, keys, passwords, `.env` contents; email addresses, machine names,
  private account names, personal drive paths.
- **The path test is "is it machine-specific?", not "is it absolute?"** (🔒 owner, 2026-10-23;
  letter H22). Not public: any path that records the owner's machine layout — a user profile, the
  project tree, other drives, scoop / AppData installs, scratchpad or session ids. Fine: repo- or
  sibling-relative paths, env vars, placeholders (`<repo-root>`), and a CONVENTION path that names
  nothing about one machine and works on any comparable host (e.g. `C:\zig-cache\<repo>` as a
  `ZIG_LOCAL_CACHE_DIR`). A scan hit is a CANDIDATE judged by that test; convention paths are not
  scrubbed.
- **Scan before committing, binaries included** (workspace C3, 2026-10-26: Zig and Rust debug info
  embedded the owner's paths in committed `.wasm` in two siblings). Run the floor's pattern with
  `git grep -a` as well as `-I`; this repo commits six binaries today, the stamped fixture
  `tests/*.wasm` (`examples/math_50.wasm` was deleted 2026-10-26; re-derived 2026-11-08 with
  `git ls-files '*.wasm'`).
- **The one email allowed is the maintainer contact** (🔒 owner, 2026-10-23 / 2026-10-26; letter
  H28), in at most three places: a registry manifest field (this repo publishes to no registry;
  `build.zig.zon` has no email field), the git commit author/committer address (used here, allowed;
  no purge needed — H25 measured no drive path in any ref, 2026-10-26), and ONE committed `cmem/`
  note naming the maintainer contact (none here). Not repeated anywhere else, memory included.
- **Private facts go in `cmem/local/`**, gitignored since 2026-10-26.
- wabt-ts and binaryen-ts are RETIRED (owner, 2026-10-26; H28): never cite `../wabt-ts/…` or
  `../binaryen-ts/…` as a location. This repo cites neither.

## No `gh` (workspace Tooling rule 9; letter H30)

- 🔒 **`gh` (the GitHub CLI) is not installed and will not be** (owner, 2026-11-08: "just checking
  the repo is easier"). What GitHub holds is read with git: `git ls-remote origin` (branches, and
  a tag's peeled `^{}` commit), `git fetch`; a public read-only API answer where git cannot say
  (the repo's topics). A task only the GitHub web UI can do — a Release page, the `zig-package`
  topic — is an **owner item**, printed as `OWNER ITEM (GitHub web UI): …` with its URL. Never
  plan around `gh`, never record anything as "blocked on gh" or "`gh` absent".
- `release.ts` and `publish.ts` are `gh`-free since 2026-11-08; the CI workflow (2026-11-09)
  calls GitHub only through `scripts/github-release.ts` (REST, `GITHUB_TOKEN`)
  ([release.md](release.md)). Check: `git grep -n -w gh -- .github
  scripts` finds nothing (2026-11-09).

## Release pages are made by CI (workspace letter H32)

- 🔒 **Every tag's GitHub Release page is made by CI, never by hand and never with `gh`** (owner,
  2026-11-09: "they both need the CI and release pages, not by hand"; "only new tags will get
  release pages" — no backfill). Here: `.github/workflows/release.yml`'s `release` job, after the
  `tag` job, in H32's order (tag exists and peels to the checked commit → an existing page is
  done → POST, read back, re-check the tag). Change it under the gate and BEFORE a release, never
  during one; it is proven only by a real release run.

## The release order: CI checks, CI tags, the page, the publish (workspace letter H33)

- 🔒 **Owner, 2026-11-10 (D1): "the CI to check first, then push the tag, and then create the
  release page from the tag, then publish the package to the registry for the project in that
  order."** No tag is ever made or pushed from the owner's machine: `release.ts` pushes `main`
  only; CI's `checks` → `tag` → `release` → `publish` run in ONE run on that push. Recorded slot by
  slot (U1–U12) in [release.md](release.md), where every release is recorded, newest first (D3).

## Bun versions (workspace Tooling rule 10, letter H31)

- 🔒 **Every Bun-version notation is the latest installed Bun** (owner, 2026-11-09: "update any
  notations on bun version to the latest installed version always"; the minimum supported Bun
  too). Measure `bun --version` before writing one: 1.4.4 on 2026-11-09. Here the notations are
  the workflow's `BUN_VERSION` and its two sha256 pins (from Bun's `SHASUMS256.txt`), moved
  together at the session after the owner installs a newer Bun. A DATED test result keeps its
  Bun until that test is re-run. Before 2026-11-09 this repo wrote no Bun version at all (H31's
  survey).

## Owner commands: "commit", "push", "release" (owner, 2026-11-10)

🔒 Written into every project by the workspace session on the owner's direction (2026-11-10:
"please write that into each project"; workspace `cmem/working-rules.md` Tooling rule 12, letter
H34). When the owner says:

- **"commit"** — first run "update the project memory files" (re-derive this repo's `cmem/` from
  its current state), then commit to the branch being worked on. Nothing is pushed.
- **"push"** — perform "commit", then merge the branch into `main`, then push `main` (and the
  working branch) to GitHub (`origin`) and read it back with `git ls-remote`.
- **"release"** — perform "push", then the release process: the workspace's
  `cmem/release-process.md` §§ 5–8 — CI checks first, then CI pushes the tag, then the GitHub
  Release page from the tag, then the publish to the registry — recorded in `cmem/release.md`
  § "Release History" (newest first, a summary of what changed). The level (patch / minor /
  major) is the owner's: ask if it was not given.

Each command includes the one before it, and none goes further than its own word (a "commit"
never pushes, a "push" never releases). This repo's own gate and merge rules apply at each step.
