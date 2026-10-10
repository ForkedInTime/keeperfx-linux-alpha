#!/usr/bin/env bash
#
# Keep linux.mk's KFX_SOURCES in step with the engine sources in the tree.
#
#   sync-linux-mk-sources.sh [--check]
#
# linux.mk lists every engine source by hand, so each week the team adds or
# deletes a file the sync's compile check failed until someone edited the list
# (four new files were waiting at the time this was written). Now that the
# Linux-only filter removes the Windows-only sources before a snapshot is
# merged, every src/*.c|cpp left in the tree is meant to be built here --
# except src/ftests/, the functional-test harness, which linux.mk never builds.
#
# New files are appended to the end of the list, so the existing order -- and
# with it C++ static-initialisation order, which follows link order -- does not
# move. Entries whose file no longer exists are dropped. Prints one line per
# change; --check only reports, and exits 1 if the list is out of date.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
cd "$LINUX_ONLY_DIR/../.."

check=0
[ "${1:-}" = --check ] && check=1
mk=linux.mk

listed="$(sed -n '/^KFX_SOURCES = \\$/,/^[^[:space:]].*[^\\]$/p' "$mk" \
    | grep -E '^src/' | sed -E 's/[[:space:]]*\\$//')"
[ -n "$listed" ] || die "could not find the KFX_SOURCES list in $mk"
present="$(git ls-files -- 'src/*.c' 'src/*.cpp' ':!src/ftests/*' | sort)"

added="$(comm -13 <(sort <<<"$listed") <(printf '%s\n' "$present"))"
dropped="$(comm -23 <(sort <<<"$listed") <(printf '%s\n' "$present"))"
[ -z "$added" ] && [ -z "$dropped" ] && { echo "linux.mk: source list is up to date"; exit 0; }
report() { local f; while IFS= read -r f; do [ -z "$f" ] || echo "linux.mk: $1 $f"; done <<<"$2"; }
report "added  " "$added"
report "dropped" "$dropped"
[ "$check" = 1 ] && exit 1

# Rebuild the list: existing entries in their order minus dropped ones, then
# the new ones, one per line, every line but the last ending in " \".
new_list="$( { grep -v -x -F -f <(printf '%s\n' "$dropped" | grep -v '^$' || true) <<<"$listed" || true
              [ -z "$added" ] || printf '%s\n' "$added"; } | grep -v '^$')"
awk -v list="$new_list" '
    BEGIN { n = split(list, items, "\n") }
    /^KFX_SOURCES = \\$/ {
        print
        for (i = 1; i <= n; i++) print items[i] (i < n ? " \\" : "")
        skipping = 1
        next
    }
    skipping {
        if ($0 !~ /\\$/) skipping = 0   # the old list ends at its first line without "\"
        next
    }
    { print }
' "$mk" > "$mk.tmp"
mv "$mk.tmp" "$mk"
