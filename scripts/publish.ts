// Publish this package to Zigistry — a SEPARATE, deliberate step, intentionally
// kept out of release.ts.
import { capture, die, has, help, readVersion, ROOT, runner, statusShort, tagExists } from "./_lib.ts";

const USAGE = `
Publish this package to Zigistry — a SEPARATE, deliberate step, intentionally
kept out of release.ts.

Zigistry has no upload/CLI publish. A library is indexed by adding the GitHub
repository topic \`zig-package\`; the repo is then (re)indexed on its next push
(typically within a day). See https://zigistry.dev/help/. This script makes
that topic present against the already-released tag, idempotently.

Prereq order:  bump-version.ts -> release.ts -> publish.ts

Usage: bun scripts/publish.ts [options]
  --yes             Don't prompt for confirmation before changing the repo.
  --dry-run         Print what would happen; change nothing.
  --allow-dirty     Skip the clean-working-tree check.
  --skip-tag-check  Don't require the v<version> tag to exist locally + on remote.
  --remote <name>   Git remote to check the tag against (default: origin).
  --topic <name>    Topic to add (default: zig-package; use \`Zig\` for programs).
  -h, --help        Show this help.

Auth-gated: needs an authenticated \`gh\` (run \`gh auth login\` or set GH_TOKEN).
Idempotent: if the topic is already set, it reports success and makes no change.
`;

process.chdir(ROOT);

let assumeYes = false;
let dryRun = false;
let allowDirty = false;
let skipTagCheck = false;
let remote = "origin";
let topic = "zig-package";

const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  const arg = args[i];
  if (arg === "--yes") assumeYes = true;
  else if (arg === "--dry-run") dryRun = true;
  else if (arg === "--allow-dirty") allowDirty = true;
  else if (arg === "--skip-tag-check") skipTagCheck = true;
  else if (arg === "--remote") remote = args[++i] ?? "";
  else if (arg === "--topic") topic = args[++i] ?? "";
  else if (arg === "-h" || arg === "--help") help(USAGE);
  else die(`unknown argument: ${arg}`, 2);
}

const run = runner(dryRun);
// Fatal in a real run; downgraded to a warning under --dry-run so the flow is inspectable.
const guard = (msg: string): void => {
  if (dryRun) console.error(`warning (ignored under --dry-run): ${msg}`);
  else die(msg);
};

// --- Version / tag (source of truth: build.zig.zon) -------------------------
const tag = `v${readVersion()}`;
console.log(`Publishing ${tag} to Zigistry (topic: ${topic})`);

// --- Preflight: clean tree --------------------------------------------------
if (!allowDirty && statusShort()) guard("working tree is dirty — commit/stash, or pass --allow-dirty.");

// --- Preflight: the release tag exists locally and on the remote ------------
if (!skipTagCheck) {
  if (!tagExists(tag)) {
    guard(`tag ${tag} not found locally — run scripts/release.ts first (or --skip-tag-check).`);
  }
  if (!capture(["git", "ls-remote", "--tags", remote, tag]).out.includes(`refs/tags/${tag}`)) {
    guard(`tag ${tag} not on ${remote} — push it (scripts/release.ts) before publishing (or --skip-tag-check).`);
  }
}

// --- Auth gate: need an authenticated gh ------------------------------------
const ghReady = has("gh") && capture(["gh", "auth", "status"]).ok;
if (!has("gh")) guard("gh CLI not found — install it, then 'gh auth login'.");
else if (!ghReady) guard("gh is not authenticated — run 'gh auth login' or set GH_TOKEN, then re-run.");

// --- Idempotency: is the topic already set? ---------------------------------
if (ghReady) {
  const view = capture(["gh", "repo", "view", "--json", "repositoryTopics"]);
  let topics: string[] = [];
  try {
    topics = (JSON.parse(view.out).repositoryTopics ?? []).map((t: { name: string }) => t.name);
  } catch { /* unreadable → treat as not set */ }
  if (topics.includes(topic)) {
    console.log(`Topic '${topic}' is already set — already published to Zigistry. Nothing to do.`);
    console.log(`(Indexing refreshes on your next push to ${remote}; allow up to ~a day.)`);
    process.exit(0);
  }
}

// --- Confirm (irreversible-ish: changes public repo metadata) ---------------
if (!assumeYes && !dryRun) {
  const reply = prompt(`Add GitHub topic '${topic}' to this repository to list it on Zigistry? [y/N]`) ?? "";
  if (!/^(y|yes)$/i.test(reply.trim())) {
    console.log("Aborted.");
    process.exit(1);
  }
}

// --- Publish: add the topic -------------------------------------------------
run(["gh", "repo", "edit", "--add-topic", topic]);

console.log(`Done: topic '${topic}' set for ${tag}.`);
console.log(`Zigistry indexes on your next push to ${remote} (typically within a day).`);
console.log(`The ${tag} tag is already pushed; if indexing doesn't pick it up, make any`);
console.log(`push to ${remote} to re-trigger it. Track listing at https://zigistry.dev.`);
