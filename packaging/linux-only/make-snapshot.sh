#!/usr/bin/env bash
#
# Make a Linux-only snapshot commit of the KeeperFX team's tree.
#
#   make-snapshot.sh <upstream-commit>
#       Takes the team's tree at <upstream-commit>, runs filter-tree.sh
#       --upstream over it, and commits the result ON TOP OF the newest
#       snapshot already in HEAD's history. Prints the new commit's sha.
#       Prints nothing (exit 0) when the filtered tree is identical to that
#       snapshot's: the team changed only Windows-only things since.
#
#   make-snapshot.sh --bootstrap <upstream-commit>
#       The first snapshot, used once when the scheme was introduced: its
#       parent is <upstream-commit> itself, which must already be merged into
#       HEAD. Merging it into the fork removes the Windows content the fork
#       still carried from earlier, direct merges of the team's history.
#
# Why snapshots and not the team's commits: merging a filtered tree means the
# merge base is the previous filtered tree, so Windows files and #ifdef blocks
# are never on either side of a merge and can never conflict -- the fork does
# not re-fight the same deletion every week. It also means the sync never
# pushes the team's own history, which contains edits to their GitHub workflow
# files that the sync bot's token is not allowed to push.
#
# The commit records where it came from in trailers (lib.sh): the team's sha,
# and its commit count, which build-number.sh uses to keep version numbers
# counting the team's commits as before.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bootstrap=0
[ "${1:-}" = --bootstrap ] && { bootstrap=1; shift; }
target="${1:-}"
[ -n "$target" ] || die "usage: $0 [--bootstrap] <upstream-commit>"
repo="$(cd "$LINUX_ONLY_DIR/../.." && pwd)"
cd "$repo"
target="$(git rev-parse --verify -q "$target^{commit}")" || die "no such commit: ${1:-}"

if [ "$bootstrap" = 1 ]; then
    [ -z "$(last_snapshot HEAD)" ] || die "HEAD already has a snapshot; --bootstrap is for the first one only"
    git merge-base --is-ancestor "$target" HEAD || die "--bootstrap needs a commit HEAD has already merged"
    parent="$target"
    since="$target"
else
    parent="$(last_snapshot HEAD)"
    [ -n "$parent" ] || die "no snapshot in HEAD's history; the first one needs --bootstrap"
    since="$(snapshot_trailer "$parent" "$SNAPSHOT_KEY")"
    [ -n "$since" ] || die "snapshot $parent has no $SNAPSHOT_KEY trailer"
fi

work="$(mktemp -d)"
trap 'git worktree remove --force "$work/tree" >/dev/null 2>&1 || true; rm -rf "$work"' EXIT
git worktree add -q --detach "$work/tree" "$target"
summary="$("$LINUX_ONLY_DIR/filter-tree.sh" --upstream "$work/tree")" \
    || die "the filter could not process $target (see above)"
tree="$(git -C "$work/tree" write-tree)"

if [ "$bootstrap" = 0 ] && [ "$tree" = "$(git rev-parse "$parent^{tree}")" ]; then
    echo "linux-only: nothing Linux-relevant changed upstream since $(git rev-parse --short "$since")" >&2
    exit 0
fi

short="$(git rev-parse --short=9 "$target")"
count="$(git rev-list --count "$target")"
{
    if [ "$bootstrap" = 1 ]; then
        echo "upstream: Linux-only snapshot of dkfans/keeperfx@$short (bootstrap)"
        echo
        echo "The team's tree as already merged, with its Windows-only files and"
        echo "#ifdef code removed. Merging it removes them from the fork."
    else
        n="$(git rev-list --count "$since..$target")"
        echo "upstream: Linux-only snapshot of dkfans/keeperfx@$short"
        echo
        echo "$n commit(s) from the team since dkfans/keeperfx@$(git rev-parse --short=9 "$since"):"
        echo
        git log --no-merges --format='- %s (%h)' "$since..$target"
    fi
    echo
    echo "Filtered with packaging/linux-only/filter-tree.sh --upstream:"
    printf '%s\n' "${summary//linux-only: /  }"
    echo
    echo "$SNAPSHOT_KEY: $target"
    echo "$SNAPSHOT_COUNT_KEY: $count"
} > "$work/msg"

git commit-tree "$tree" -p "$parent" -F "$work/msg"
