#!/usr/bin/env bash
# Checks that the save format's field tables list every member of every saved
# struct, with this edition's host compiler.
#
# Since upstream #5424 a saved game holds the fields its tables
# (src/kfx/save/core/schema/) name, read back by name. A struct member added
# without a table entry is not saved at all, and nothing fails: the game just
# forgets it on load. upstream checks this with tools/save-tables/check_tables.sh,
# which reads the struct layout through a 32-bit MinGW compiler and
# gdb-multiarch. This does the same with the native gcc and gdb (the check
# compares member names, not sizes, so the 64-bit layout is as good), using
# upstream's own probe list, gdb walker and table checker unchanged.
#
#   packaging/ci/check-save-tables.sh
#
# Prints paste-ready table lines for anything missing and exits non-zero when a
# table is wrong. Needs gcc, gdb, python3 and the SDL3 headers.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
layout="$root/tools/save-layout"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

roots=""
{
    echo '#include "pre_inc.h"'
    while read -r kw a _; do
        [ "$kw" = "include" ] && echo "#include \"$a\""
    done < "$layout/roots.txt"
    echo '#include "post_inc.h"'
    n=0
    while read -r kw a _; do
        [ "$kw" = "root" ] || continue
        roots="$roots $a"
        if [[ "$a" == typedef:* ]]; then
            echo "${a#typedef:} probe_root_$n;"
        else
            echo "struct $a probe_root_$n;"
        fi
        n=$((n + 1))
    done < "$layout/roots.txt"
    echo "$roots" > "$work/roots"
} > "$work/probe.c"
roots="$(cat "$work/roots")"

# shellcheck disable=SC2046
gcc -g -c -w -I"$layout/stub" -I"$root/src" -I"$root/deps/centitoml" \
    $(pkg-config --cflags-only-I sdl3 2>/dev/null) \
    -DBFDEBUG_LEVEL=0 -DDEBUG=0 "$work/probe.c" -o "$work/probe.o"

gdb -batch -nx \
    -ex "python ROOTS='$roots'; OUTFILE='$work/types.tsv'" \
    -ex "source $root/tools/save-tables/dump_types.py" \
    "$work/probe.o" > /dev/null

if grep -q $'\tmissing\t' "$work/types.tsv"; then
    echo "error: saved type not found in the headers:" >&2
    grep $'\tmissing\t' "$work/types.tsv" | cut -f1 >&2
    exit 1
fi
python3 "$root/tools/save-tables/tables.py" check "$work/types.tsv" "$root/src/kfx/save/core/schema"
