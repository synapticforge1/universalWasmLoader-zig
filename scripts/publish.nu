#!/usr/bin/env nu
# Publish this package to Zigistry — a SEPARATE, deliberate step (cross-platform
# sibling of publish.sh), intentionally kept out of release.nu.
#
# Zigistry has no upload/CLI publish. A library is indexed by adding the GitHub
# repository topic `zig-package`; the repo is then (re)indexed on its next push
# (typically within a day). See https://zigistry.dev/help/. This makes that topic
# present against the already-released tag, idempotently.
#
# Prereq order:  bump-version.nu -> release.nu -> publish.nu
#
# Usage: nu scripts/publish.nu [flags]
# Auth-gated: needs an authenticated `gh` (run `gh auth login` or set GH_TOKEN).

# Read the version from build.zig.zon (single source of truth).
def read-version []: nothing -> string {
  let parsed = (open build.zig.zon | parse --regex '\.version\s*=\s*"(?<v>[^"]+)"')
  if ($parsed | is-empty) {
    print -e "error: could not read .version from build.zig.zon"
    exit 1
  }
  $parsed | get v.0
}

# Fatal in a real run; downgraded to a warning under --dry-run so the flow is inspectable.
def guard [dry: bool, msg: string] {
  if $dry {
    print -e $"warning \(ignored under --dry-run): ($msg)"
  } else {
    print -e $"error: ($msg)"
    exit 1
  }
}

def gh-authed []: nothing -> bool {
  if (which gh | is-empty) { return false }
  (^gh auth status | complete).exit_code == 0
}

def main [
  --yes             # Don't prompt for confirmation before changing the repo
  --dry-run         # Print what would happen; change nothing
  --allow-dirty     # Skip the clean-working-tree check
  --skip-tag-check  # Don't require the v<version> tag locally + on remote
  --remote: string = "origin"
  --topic: string = "zig-package"   # use `Zig` for programs
] {
  let version = (read-version)
  let tag = $"v($version)"
  print $"Publishing ($tag) to Zigistry \(topic: ($topic))"

  # Preflight: clean tree.
  if not $allow_dirty {
    let status = (^git status --porcelain)
    if ($status | str trim | is-not-empty) {
      guard $dry_run "working tree is dirty — commit/stash, or pass --allow-dirty."
    }
  }

  # Preflight: the release tag exists locally and on the remote.
  if not $skip_tag_check {
    if (^git rev-parse -q --verify $"refs/tags/($tag)" | complete).exit_code != 0 {
      guard $dry_run $"tag ($tag) not found locally — run scripts/release.nu first \(or --skip-tag-check)."
    }
    let lsr = (^git ls-remote --tags $remote $tag | complete)
    if not ($lsr.stdout | str contains $"refs/tags/($tag)") {
      guard $dry_run $"tag ($tag) not on ($remote) — push it \(scripts/release.nu) before publishing \(or --skip-tag-check)."
    }
  }

  # Auth gate: need an authenticated gh.
  if (which gh | is-empty) {
    guard $dry_run "gh CLI not found — install it, then 'gh auth login'."
  } else if (^gh auth status | complete).exit_code != 0 {
    guard $dry_run "gh is not authenticated — run 'gh auth login' or set GH_TOKEN, then re-run."
  }

  # Idempotency: is the topic already set?
  mut already = false
  if (gh-authed) {
    let topics = (^gh repo view --json repositoryTopics --jq '.repositoryTopics[].name' | complete)
    if ($topics.exit_code == 0) and ($topics.stdout | lines | any {|t| $t == $topic }) {
      $already = true
    }
  }
  if $already {
    print $"Topic '($topic)' is already set — already published to Zigistry. Nothing to do."
    print $"\(Indexing refreshes on your next push to ($remote); allow up to ~a day.)"
    exit 0
  }

  # Confirm (irreversible-ish: changes public repo metadata).
  if (not $yes) and (not $dry_run) {
    let reply = (input $"Add GitHub topic '($topic)' to this repository to list it on Zigistry? [y/N] ")
    if not (($reply | str downcase | str trim) in ["y" "yes"]) {
      print "Aborted."
      exit 1
    }
  }

  # Publish: add the topic.
  print $"+ gh repo edit --add-topic ($topic)"
  if not $dry_run { ^gh repo edit --add-topic $topic }

  print $"Done: topic '($topic)' set for ($tag)."
  print $"Zigistry indexes on your next push to ($remote) \(typically within a day)."
  print $"The ($tag) tag is already pushed; if indexing doesn't pick it up, make any"
  print $"push to ($remote) to re-trigger it. Track listing at https://zigistry.dev."
}
