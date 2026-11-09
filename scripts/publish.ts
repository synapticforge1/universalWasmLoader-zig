// List this package on Zigistry — a SEPARATE, deliberate step, intentionally
// kept out of release.ts. Changes nothing: the one action, a repository topic,
// is a GitHub web-UI task, printed as an OWNER ITEM (no GitHub CLI, workspace
// Tooling rule 9).
import { die, githubSlug, help, ownerItem, readVersion, remoteCommit, revParse, ROOT, statusShort, tagExists } from "./_lib.ts";

const USAGE = `
List this package on Zigistry — a SEPARATE, deliberate step, intentionally
kept out of release.ts.

Zigistry has no upload/CLI publish. A library is indexed by giving the GitHub
repository the topic \`zig-package\`; the repo is then (re)indexed on its next
push (typically within a day). See https://zigistry.dev/help/.

This script CHANGES NOTHING. It checks that the v<version> tag exists locally
and on the remote (git ls-remote, at the same commit), reads the repository's
topics from GitHub's public REST API (read-only, no token), and, when the topic
is missing, prints the web-UI step as an OWNER ITEM.

Prereq order:  bump-version.ts -> release.ts -> publish.ts

Usage: bun scripts/publish.ts [options]
  --allow-dirty     Skip the clean-working-tree check.
  --skip-tag-check  Don't require the v<version> tag locally + on the remote.
  --remote <name>   Git remote to check the tag against (default: origin).
  --topic <name>    Topic to look for (default: zig-package; \`Zig\` for programs).
  -h, --help        Show this help.

Exit: 0 when the topic is set (listed) or the owner item was printed;
1 when a check fails.
`;

process.chdir(ROOT);

let allowDirty = false;
let skipTagCheck = false;
let remote = "origin";
let topic = "zig-package";

const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  const arg = args[i];
  if (arg === "--allow-dirty") allowDirty = true;
  else if (arg === "--skip-tag-check") skipTagCheck = true;
  else if (arg === "--remote") remote = args[++i] ?? "";
  else if (arg === "--topic") topic = args[++i] ?? "";
  else if (arg === "-h" || arg === "--help") help(USAGE);
  else die(`unknown argument: ${arg}`, 2);
}

// --- Version / tag (source of truth: build.zig.zon) -------------------------
const tag = `v${readVersion()}`;
console.log(`Zigistry listing for ${tag} (topic: ${topic})`);

if (!allowDirty && statusShort()) die("working tree is dirty — commit/stash, or pass --allow-dirty.");

// --- The release tag exists locally and on the remote, at one commit ---------
if (!skipTagCheck) {
  if (!tagExists(tag)) die(`tag ${tag} not found locally — run scripts/release.ts first (or --skip-tag-check).`);
  const local = revParse(`${tag}^{commit}`);
  const onRemote = remoteCommit(remote, `refs/tags/${tag}`);
  if (onRemote === null) die(`tag ${tag} not on ${remote} — push it (scripts/release.ts) first (or --skip-tag-check).`);
  if (onRemote !== local) die(`tag ${tag} is ${local} here but ${onRemote} on ${remote}.`);
  console.log(`tag ${tag} = ${local}, here and on ${remote}`);
}

const slug = githubSlug(remote);
if (!slug) die(`${remote} is not a github.com remote; Zigistry indexes GitHub repositories.`);

// --- Is the topic set? (read-only public REST API; unknown is not "set") ----
let topics: string[] | null = null;
try {
  const r = await fetch(`https://api.github.com/repos/${slug}`, {
    headers: { Accept: "application/vnd.github+json", "User-Agent": "universalWasmLoader-zig-publish" },
  });
  if (r.ok) topics = ((await r.json()) as { topics?: string[] }).topics ?? [];
  else console.log(`(GitHub answered HTTP ${r.status} for ${slug}; topics unknown)`);
} catch (e) {
  console.log(`(could not reach GitHub's API: ${e instanceof Error ? e.message : e}; topics unknown)`);
}

if (topics?.includes(topic)) {
  console.log(`Topic '${topic}' is set on ${slug} — listed on Zigistry. Nothing to do.`);
  console.log(`(Indexing refreshes on the next push to ${remote}; allow up to ~a day.)`);
  process.exit(0);
}

ownerItem(`add the repository topic '${topic}' to list ${slug} on Zigistry`, `https://github.com/${slug}`, [
  `About (the gear beside it) -> Topics -> add '${topic}' -> Save changes.`,
  topics === null ? "(Whether it is already set could not be read.)" : `Topics today: ${topics.length ? topics.join(", ") : "none"}.`,
  `Zigistry then indexes on the next push to ${remote} (typically within a day): https://zigistry.dev`,
]);
