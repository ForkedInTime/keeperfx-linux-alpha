# Shared helpers for the Linux-only filter. Sourced, not executed.
# shellcheck shell=bash
# Variables defined here are used by the scripts that source this file.
# shellcheck disable=SC2034

LINUX_ONLY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOVE_LIST="$LINUX_ONLY_DIR/remove.list"
MACROS_FILE="$LINUX_ONLY_DIR/windows-macros.txt"

# Source files unifdef is run over. Deliberately C/C++ only: shell, Python and
# Lua have no preprocessor, and their platform checks are not ours to rewrite.
C_LIKE_PATHSPECS=(':(glob)**/*.c' ':(glob)**/*.h' ':(glob)**/*.cpp' ':(glob)**/*.hpp'
                  ':(glob)**/*.cc' ':(glob)**/*.cxx' ':(glob)**/*.hh' ':(glob)**/*.hxx'
                  ':(glob)**/*.inl')

die() { echo "linux-only: $*" >&2; exit 2; }

# Strip comments and blanks from a list file.
_list_entries() { sed -e 's/#.*//' -e 's/[[:space:]]*$//' -e '/^[[:space:]]*$/d' "$1"; }

# remove_patterns <section>: the raw patterns of one [section] of remove.list.
remove_patterns() {
    local want="$1" section="" line
    while IFS= read -r line; do
        if [[ $line =~ ^\[([a-z-]+)\]$ ]]; then
            section="${BASH_REMATCH[1]}"
            continue
        fi
        [[ $section == "$want" ]] && printf '%s\n' "$line"
    done < <(_list_entries "$REMOVE_LIST")
}

# pattern_to_pathspec <pattern>: git glob pathspec for one remove.list entry.
# A trailing "/" names a whole directory.
pattern_to_pathspec() {
    local p="$1"
    if [[ $p == */ ]]; then
        printf ':(glob)%s**\n' "$p"
    else
        printf ':(glob)%s\n' "$p"
    fi
}

# pathspecs <section>...: pathspecs for every pattern in the given sections.
pathspecs() {
    local s p
    for s in "$@"; do
        while IFS= read -r p; do pattern_to_pathspec "$p"; done < <(remove_patterns "$s")
    done
}

# windows_macros: one symbol per line.
windows_macros() { _list_entries "$MACROS_FILE"; }

# unifdef_args: -U<sym> for every Windows symbol.
unifdef_args() {
    local m
    while IFS= read -r m; do printf -- '-U%s\n' "$m"; done < <(windows_macros)
}

# macro_regex: an ERE alternation matching any Windows symbol as a whole word.
macro_regex() { windows_macros | paste -sd'|' | sed 's/.*/\\b(&)\\b/'; }

# require_git_worktree <dir>
require_git_worktree() {
    git -C "$1" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
        || die "$1 is not a git work tree (the filter works on tracked files)"
}

# ---- snapshots ---------------------------------------------------------------
# The weekly sync merges filtered snapshots of the team's tree, never their
# commits directly (see README.md). Each snapshot commit carries two trailers:
#   Linux-Snapshot-Of: <full sha of the team's commit it was made from>
#   Linux-Snapshot-Upstream-Count: <git rev-list --count of that commit>
SNAPSHOT_KEY="Linux-Snapshot-Of"
SNAPSHOT_COUNT_KEY="Linux-Snapshot-Upstream-Count"

# last_snapshot [rev]: the newest snapshot commit reachable from rev (HEAD).
last_snapshot() {
    git rev-list --topo-order -n1 --grep="^$SNAPSHOT_KEY: " "${1:-HEAD}"
}

# first_snapshot [rev]: the bootstrap snapshot, the oldest one reachable.
# (tail, not --reverse | head: head exiting early would SIGPIPE under pipefail.)
first_snapshot() {
    git rev-list --topo-order --grep="^$SNAPSHOT_KEY: " "${1:-HEAD}" | tail -n1
}

# snapshot_trailer <commit> <key>: one trailer's value.
snapshot_trailer() {
    git log -1 --format=%B "$1" | sed -n "s/^$2: *//p" | tail -n1
}
