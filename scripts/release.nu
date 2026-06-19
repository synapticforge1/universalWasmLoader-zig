#!/usr/bin/env nu
# Cut a release of universalWasmLoader-zig consistently (cross-platform).
#
# Sibling of release.sh. The single source of truth for the version is
# `.version` in build.zig.zon. This tags the current commit `v<version>`, pushes
# the branch and tag to `origin`, and (unless --no-release) creates the matching
# GitHub Release. It does NOT publish to Zigistry — that's publish.nu.
#
# Usage: nu scripts/release.nu [flags]

# Read the version from build.zig.zon (single source of truth).
def read-version []: nothing -> string {
  let parsed = (open build.zig.zon | parse --regex '\.version\s*=\s*"(?<v>[^"]+)"')
  if ($parsed | is-empty) {
    print -e "error: could not read .version from build.zig.zon"
    exit 1
  }
  $parsed | get v.0
}

def main [
  --notes: string = ""    # Release notes body (default: a generic line)
  --no-release            # Push the tag only; skip creating a GitHub Release
  --no-build              # Skip the cold-cache `zig build run` verification
  --remote: string = "origin"
  --dry-run               # Print what would happen; make no tags/pushes/releases
] {
  let version = (read-version)
  let tag = $"v($version)"
  print $"Releasing ($tag) \(from build.zig.zon)"

  # Preflight: clean tree.
  let status = (^git status --porcelain)
  if ($status | str trim | is-not-empty) {
    print -e "error: working tree is dirty — commit or stash before releasing."
    print -e $status
    exit 1
  }

  let branch = (^git rev-parse --abbrev-ref HEAD | str trim)
  print $"On branch: ($branch)"

  # If the tag already exists, it must point at the current commit.
  let head = (^git rev-parse HEAD | str trim)
  let tag_exists = (^git rev-parse -q --verify $"refs/tags/($tag)" | complete).exit_code == 0
  if $tag_exists {
    let tagcommit = (^git rev-parse $"($tag)^{commit}" | str trim)
    if $tagcommit != $head {
      print -e $"error: tag ($tag) exists but does not point at HEAD."
      print -e "       Bump .version in build.zig.zon or move the tag deliberately."
      exit 1
    }
    print $"tag ($tag) already exists at HEAD — reusing."
  }

  # Verify it builds (cold cache).
  if not $no_build {
    print "Verifying build (cold cache)…"
    print "+ rm -rf .zig-cache"
    if not $dry_run { rm --recursive --force .zig-cache }
    print "+ zig build run"
    if not $dry_run { ^zig build run }
  }

  # Tag.
  if not $tag_exists {
    print $"+ git tag -a ($tag) -m \"universalWasmLoader-zig ($tag)\""
    if not $dry_run { ^git tag -a $tag -m $"universalWasmLoader-zig ($tag)" }
  }

  # Push branch + tag.
  print $"+ git push ($remote) ($branch)"
  if not $dry_run { ^git push $remote $branch }
  print $"+ git push ($remote) ($tag)"
  if not $dry_run { ^git push $remote $tag }

  # GitHub Release.
  if not $no_release {
    if (which gh | is-empty) {
      print -e "warning: gh CLI not found — skipping GitHub Release (tag is pushed)."
    } else if (^gh release view $tag | complete).exit_code == 0 {
      print $"GitHub Release ($tag) already exists — leaving it untouched."
    } else {
      let body = (if ($notes | is-empty) {
        $"Release ($tag) of universalWasmLoader-zig — idiomatic Zig binding for the Universal WASM Loader."
      } else { $notes })
      print $"+ gh release create ($tag) --title ($tag) --notes <body>"
      if not $dry_run { ^gh release create $tag --title $tag --notes $body }
    }
  }

  print $"Done: ($tag)"
}
