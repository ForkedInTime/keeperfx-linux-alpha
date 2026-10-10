#!/usr/bin/env bash
#
# Print the build number for the commit checked out (the 4th part of
# 1.4.0.NNNN). Every build -- CI, the AUR PKGBUILD, refresh-alpha.sh -- uses
# this, so a version means the same thing wherever it was built.
#
# It used to be `git rev-list --count HEAD`, which counted the team's commits
# because their history was merged in directly. Since the switch to Linux-only
# snapshots (see README.md) only snapshot commits come in, so the count alone
# would grow more slowly than the team's -- and keeperfx-launcher-qt enables
# settings by build number, measured against the team's numbering. So the
# team's commits are added back from the snapshot trailers:
#
#   rev-list --count HEAD
#     + (team's commit count at the newest snapshot
#        - team's commit count at the first snapshot, which HEAD already counts)
#
# Before the first snapshot this is exactly rev-list --count HEAD. It only
# grows: every commit adds one, and every snapshot adds the team's new commits.
# Needs full history (makepkg's git sources and fetch-depth: 0 checkouts have it).
set -euo pipefail
. "$(dirname "$0")/lib.sh"
cd "$LINUX_ONLY_DIR/../.."

[ "$(git rev-parse --is-shallow-repository)" = false ] \
    || die "shallow clone: the build number needs full history (git fetch --unshallow)"
count="$(git rev-list --count HEAD)"
newest="$(last_snapshot HEAD)"
if [ -z "$newest" ]; then
    echo "$count"
    exit 0
fi
first="$(first_snapshot HEAD)"
newest_up="$(snapshot_trailer "$newest" "$SNAPSHOT_COUNT_KEY")"
first_up="$(snapshot_trailer "$first" "$SNAPSHOT_COUNT_KEY")"
[[ $newest_up =~ ^[0-9]+$ && $first_up =~ ^[0-9]+$ ]] \
    || die "snapshot commits lack a numeric $SNAPSHOT_COUNT_KEY trailer"
echo $((count + newest_up - first_up))
