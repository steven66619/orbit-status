#!/bin/bash
# update.sh — release a new version of an orbit-suite package with a codename.
#
# Usage:
#   ./update.sh <codename>                      # release orbit-status (auto version)
#   ./update.sh <package> <codename>            # release a specific package
#   ./update.sh <package> <version> <codename>  # release with explicit version
#   ./update.sh rename <new-codename>           # rename latest orbit-status codename
#   ./update.sh rename <package> <new-codename> # rename a specific package
#
# Examples:
#   ./update.sh pulsar                          # orbit-status 1.9 "Pulsar"
#   ./update.sh orbiter columbia                # orbiter 1.1.0 "Columbia"
#   ./update.sh realspeed-cli 1.2.0 dash        # realspeed-cli 1.2.0 "Dash"
#   ./update.sh rename aurora                   # rename latest orbit-status codename
#
# What it does (release):
#   1. Determines the next version (auto minor bump, or explicit)
#   2. Validates the codename (lowercase, unused)
#   3. Creates + pushes the v<version>-<codename> tag on the source repo
#   4. Triggers the bump-versions workflow, which rebuilds + redeploys
#      the package repo to GitHub Pages
#
# What it does (rename):
#   1. Finds the latest v<version>-<codename> tag on the source repo
#   2. Validates the new codename (lowercase, unused)
#   3. Re-tags the same commit as v<version>-<new-codename>, deletes the old tag
#   4. Renames the GitHub release, then triggers the bump-versions workflow
#
# Requires: git, gh (authenticated), network access.

set -euo pipefail

REPO_OWNER="steven66619"
PKG_REPO="orbit-status"   # repo that hosts the PKGBUILDs
WORKFLOW="Bump package versions on new releases"

# package -> source repo (where the release tags live)
declare -A SOURCE_REPO=(
  [orbit-status]=orbit-status
  [orbiter]=orbiter
  [realspeed-cli]=realspeed-cli
)

say() { printf '\n\033[1;34m== %s ==\033[0m\n' "$*"; }
die() { printf '\033[1;31mError: %s\033[0m\n' "$*" >&2; exit 1; }

# --- rename mode ---------------------------------------------------------------
if [ "${1:-}" = "rename" ]; then
  case "$#" in
    2) pkg="orbit-status"; new_codename="$2" ;;
    3) pkg="$2"; new_codename="$3" ;;
    *) die "Usage: $0 rename [package] <new-codename>" ;;
  esac
  [ -n "${SOURCE_REPO[$pkg]:-}" ] || die "Unknown package '$pkg'. Known: ${!SOURCE_REPO[@]}"
  [[ "$new_codename" =~ ^[a-z][a-z0-9]*$ ]] || die "Codename must be lowercase alphanumeric (got '$new_codename')"
  src="${SOURCE_REPO[$pkg]}"

  # latest release tag, e.g. v1.8-nebula
  latest_tag=$(gh api "repos/$REPO_OWNER/$src/tags?per_page=100" --jq '.[].name' \
    | grep -E '^v[0-9]' | sort -V | tail -1)
  [ -n "$latest_tag" ] || die "No version tags found on $REPO_OWNER/$src"
  version=$(printf '%s' "$latest_tag" | sed 's/^v//; s/-.*//')
  old_codename=$(printf '%s' "$latest_tag" | sed -n 's/^v[0-9.]*-//p')
  [ -n "$old_codename" ] || die "Latest tag $latest_tag has no codename"
  [ "$old_codename" != "$new_codename" ] || die "Already using codename '$new_codename'"

  existing=$(gh api "repos/$REPO_OWNER/$src/tags?per_page=100" --jq '.[].name' 2>/dev/null || true)
  if grep -qE "(^|-)${new_codename}$" <<< "$existing"; then
    die "Codename '$new_codename' already used on $REPO_OWNER/$src"
  fi

  say "Renaming $pkg $version: '$old_codename' -> '$new_codename'"
  TMP="$(mktemp -d /tmp/update.XXXXXX)"
  trap 'rm -rf "$TMP"' EXIT
  git clone --quiet "https://github.com/$REPO_OWNER/$src.git" "$TMP"
  sha=$(git -C "$TMP" rev-parse "$latest_tag^{commit}")
  git -C "$TMP" tag "v${version}-${new_codename}" "$sha"
  git -C "$TMP" push origin "v${version}-${new_codename}"

  # rename the GitHub release before the old tag disappears
  release_id=$(gh api "repos/$REPO_OWNER/$src/releases/tags/$latest_tag" --jq '.id' 2>/dev/null || true)
  if [ -n "$release_id" ]; then
    gh api -X PATCH "repos/$REPO_OWNER/$src/releases/$release_id" \
      -f name="$version '$new_codename'" >/dev/null
    say "Renamed GitHub release to \"$version '$new_codename'\""
  fi

  git -C "$TMP" push origin ":$latest_tag"   # delete old remote tag
  git -C "$TMP" tag -d "$latest_tag"         # delete old local tag

  say "Triggering '$WORKFLOW'"
  gh workflow run "$WORKFLOW" --repo "$REPO_OWNER/$PKG_REPO"

  say "Done! $pkg $version is now '$new_codename' (tag v${version}-${new_codename})."
  say "If you already have the old package installed, reinstall it:"
  say "  sudo pacman -S $pkg"
  exit 0
fi

# --- release mode: parse args --------------------------------------------------
case "$#" in
  1) pkg="orbit-status"; version=""; codename="$1" ;;
  2) pkg="$1"; version=""; codename="$2" ;;
  3) pkg="$1"; version="$2"; codename="$3" ;;
  *) die "Usage: $0 [package] [version] <codename>" ;;
esac

[ -n "${SOURCE_REPO[$pkg]:-}" ] || die "Unknown package '$pkg'. Known: ${!SOURCE_REPO[@]}"
[[ "$codename" =~ ^[a-z][a-z0-9]*$ ]] || die "Codename must be lowercase alphanumeric (got '$codename')"

# --- determine version ---------------------------------------------------------
if [ -z "$version" ]; then
  current=$(curl -fsSL "https://raw.githubusercontent.com/$REPO_OWNER/$PKG_REPO/main/packages/$pkg/PKGBUILD" \
    | sed -n 's/^pkgver=//p')
  [ -n "$current" ] || die "Could not read current pkgver for $pkg"
  # pkgver carries the codename (1.8_nebula); strip it for the numeric version.
  current=${current%%_*}
  # auto minor bump: 1.7 -> 1.8, 1.0.5 -> 1.1.0
  IFS='.' read -r major minor patch <<< "$current"
  if [ -n "$patch" ]; then
    version="${major}.$((minor+1)).0"
  else
    version="${major}.$((minor+1))"
  fi
  say "Auto version: $current -> $version"
fi

tag="v${version}-${codename}"
src="${SOURCE_REPO[$pkg]}"

# --- validate codename ---------------------------------------------------------
existing=$(gh api "repos/$REPO_OWNER/$src/tags?per_page=100" --jq '.[].name' 2>/dev/null || true)
if grep -qx "$tag" <<< "$existing"; then
  die "Tag $tag already exists on $REPO_OWNER/$src"
fi
# Catch the codename both as a bare tag and embedded in v<ver>-<codename> tags.
if grep -qE "(^|-)${codename}$" <<< "$existing"; then
  die "Codename '$codename' already used on $REPO_OWNER/$src"
fi

# --- create + push tag ---------------------------------------------------------
say "Creating tag $tag on $REPO_OWNER/$src"
TMP="$(mktemp -d /tmp/update.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
git clone --quiet "https://github.com/$REPO_OWNER/$src.git" "$TMP"
git -C "$TMP" tag "$tag"
git -C "$TMP" push origin "$tag"

# --- trigger bump workflow -----------------------------------------------------
say "Triggering '$WORKFLOW'"
gh workflow run "$WORKFLOW" --repo "$REPO_OWNER/$PKG_REPO"

say "Done! $pkg $version '$codename' tagged as $tag."
say "The bump workflow will rebuild the package; check it at:"
say "  https://github.com/$REPO_OWNER/$PKG_REPO/actions"