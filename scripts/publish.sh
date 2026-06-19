#!/usr/bin/env bash
# Publish this package to Zigistry — a SEPARATE, deliberate step, intentionally
# kept out of release.sh.
#
# Zigistry has no upload/CLI publish. A library is indexed by adding the GitHub
# repository topic `zig-package`; the repo is then (re)indexed on its next push
# (typically within a day). See https://zigistry.dev/help/. This script makes
# that topic present against the already-released tag, idempotently.
#
# Prereq order:  scripts/bump-version.sh -> scripts/release.sh -> scripts/publish.sh
#
# Usage: scripts/publish.sh [options]
#   --yes             Don't prompt for confirmation before changing the repo.
#   --dry-run         Print what would happen; change nothing.
#   --allow-dirty     Skip the clean-working-tree check.
#   --skip-tag-check  Don't require the v<version> tag to exist locally + on remote.
#   --remote <name>   Git remote to check the tag against (default: origin).
#   --topic <name>    Topic to add (default: zig-package; use `Zig` for programs).
#   -h, --help        Show this help.
#
# Auth-gated: needs an authenticated `gh` (run `gh auth login` or set GH_TOKEN).
# Idempotent: if the topic is already set, it reports success and makes no change.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

ASSUME_YES=0
DRY_RUN=0
ALLOW_DIRTY=0
SKIP_TAG_CHECK=0
REMOTE="origin"
TOPIC="zig-package"

while [ $# -gt 0 ]; do
  case "$1" in
    --yes)            ASSUME_YES=1; shift ;;
    --dry-run)        DRY_RUN=1; shift ;;
    --allow-dirty)    ALLOW_DIRTY=1; shift ;;
    --skip-tag-check) SKIP_TAG_CHECK=1; shift ;;
    --remote)         REMOTE="${2:-}"; shift 2 ;;
    --topic)          TOPIC="${2:-}"; shift 2 ;;
    -h|--help)        sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; s/^#//'; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done

run() { echo "+ $*"; if [ "$DRY_RUN" -eq 0 ]; then "$@"; fi; }
# Fatal in a real run; downgraded to a warning under --dry-run so the flow is inspectable.
guard() { # guard <message>
  if [ "$DRY_RUN" -eq 1 ]; then echo "warning (ignored under --dry-run): $1" >&2; return 0; fi
  echo "error: $1" >&2; exit 1
}

# --- Version / tag (source of truth: build.zig.zon) -------------------------
VERSION="$(grep -oE '\.version[[:space:]]*=[[:space:]]*"[^"]+"' build.zig.zon \
           | grep -oE '"[^"]+"' | tr -d '"')"
[ -n "$VERSION" ] || { echo "error: could not read .version from build.zig.zon" >&2; exit 1; }
TAG="v$VERSION"
echo "Publishing $TAG to Zigistry (topic: $TOPIC)"

# --- Preflight: clean tree --------------------------------------------------
if [ "$ALLOW_DIRTY" -eq 0 ] && [ -n "$(git status --porcelain)" ]; then
  guard "working tree is dirty — commit/stash, or pass --allow-dirty."
fi

# --- Preflight: the release tag exists locally and on the remote ------------
if [ "$SKIP_TAG_CHECK" -eq 0 ]; then
  git rev-parse -q --verify "refs/tags/$TAG" >/dev/null \
    || guard "tag $TAG not found locally — run scripts/release.sh first (or --skip-tag-check)."
  if ! git ls-remote --tags "$REMOTE" "$TAG" | grep -q "refs/tags/$TAG"; then
    guard "tag $TAG not on $REMOTE — push it (scripts/release.sh) before publishing (or --skip-tag-check)."
  fi
fi

# --- Auth gate: need an authenticated gh ------------------------------------
if ! command -v gh >/dev/null 2>&1; then
  guard "gh CLI not found — install it, then 'gh auth login'."
elif ! gh auth status >/dev/null 2>&1; then
  guard "gh is not authenticated — run 'gh auth login' or set GH_TOKEN, then re-run."
fi

# --- Idempotency: is the topic already set? ---------------------------------
ALREADY=0
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
  if gh repo view --json repositoryTopics \
       --jq '.repositoryTopics[].name' 2>/dev/null | grep -qx "$TOPIC"; then
    ALREADY=1
  fi
fi
if [ "$ALREADY" -eq 1 ]; then
  echo "Topic '$TOPIC' is already set — already published to Zigistry. Nothing to do."
  echo "(Indexing refreshes on your next push to $REMOTE; allow up to ~a day.)"
  exit 0
fi

# --- Confirm (irreversible-ish: changes public repo metadata) ---------------
if [ "$ASSUME_YES" -eq 0 ] && [ "$DRY_RUN" -eq 0 ]; then
  printf "Add GitHub topic '%s' to this repository to list it on Zigistry? [y/N] " "$TOPIC"
  read -r reply
  case "$reply" in [yY]|[yY][eE][sS]) ;; *) echo "Aborted."; exit 1 ;; esac
fi

# --- Publish: add the topic -------------------------------------------------
run gh repo edit --add-topic "$TOPIC"

echo "Done: topic '$TOPIC' set for $TAG."
echo "Zigistry indexes on your next push to $REMOTE (typically within a day)."
echo "The $TAG tag is already pushed; if indexing doesn't pick it up, make any"
echo "push to $REMOTE to re-trigger it. Track listing at https://zigistry.dev."
