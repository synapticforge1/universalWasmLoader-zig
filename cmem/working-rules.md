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
  `git grep -a` as well as `-I`; this repo commits one binary today, `examples/math_50.wasm`.
- **The one email allowed is the maintainer contact** (🔒 owner, 2026-10-23 / 2026-10-26; letter
  H28), in at most three places: a registry manifest field (this repo publishes to no registry;
  `build.zig.zon` has no email field), the git commit author/committer address (used here, allowed;
  no purge needed — H25 measured no drive path in any ref, 2026-10-26), and ONE committed `cmem/`
  note naming the maintainer contact (none here). Not repeated anywhere else, memory included.
- **Private facts go in `cmem/local/`**, gitignored since 2026-10-26.
- wabt-ts and binaryen-ts are RETIRED (owner, 2026-10-26; H28): never cite `../wabt-ts/…` or
  `../binaryen-ts/…` as a location. This repo cites neither.
