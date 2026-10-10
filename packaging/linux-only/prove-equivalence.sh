#!/usr/bin/env bash
#
# Prove that the Linux-only filter does not change what the Linux compiler sees.
#
#   prove-equivalence.sh --upstream|--fork <commit>
#
# Checks out <commit> twice: once untouched, once run through
#   filter-tree.sh --keep-lines
# (identical to the real filter except that removed lines become blank lines,
# so __LINE__ and every line marker stay put). Then every C/C++ file the Linux
# build compiles is run through the preprocessor in both trees, with linux.mk's
# own flags, and the two outputs must be byte-identical. Equal preprocessor
# output means equal compiler input, so the filter cannot have changed the
# game. Files the filter deletes outright must not be needed: if one of them is
# #included by a compiled file, the filtered tree fails to preprocess and the
# proof fails.
#
# Must run from a checkout of this fork that has built its dependencies once
# (`make -f linux.mk deps/centijson/include/json.h deps/astronomy/include/astronomy.h
#  deps/enet6/include/enet6/enet.h deps/libcurl/lib/libcurl.a src/ver_defs.h`)
# with the SDL3 environment loaded (packaging/ci/build-sdl3.sh --env).
#
# Compiled files: linux.mk's KFX_SOURCES, TOML_SOURCES and the glad loader --
# and, for a snapshot of the team's tree, also any other src/*.c|cpp it contains
# (a file they add this week is not in linux.mk yet). Exit 0 = proven, 1 = a
# difference (printed), 2 = error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

mode="${1:-}" commit="${2:-}"
case "$mode" in --upstream|--fork) ;; *) die "usage: $0 --upstream|--fork <commit>" ;; esac
[ -n "$commit" ] || die "usage: $0 --upstream|--fork <commit>"
repo="$(cd "$LINUX_ONLY_DIR/../.." && pwd)"
cd "$repo"
git rev-parse --verify -q "$commit^{commit}" >/dev/null || die "no such commit: $commit"
for d in deps/centijson deps/astronomy deps/enet6 deps/libcurl; do
    [ -d "$d" ] || die "$d is missing; build the dependencies first (see the header)"
done
[ -f src/ver_defs.h ] || die "src/ver_defs.h is missing; run make -f linux.mk src/ver_defs.h"

mk() { make -s -f linux.mk "print-$1"; }
strip_flags() { tr ' ' '\n' | grep -v -E '^(-MMD|-MP|-flto.*|-c)$' | grep -v '^$' | paste -sd' '; }
cflags="$(mk KFX_CFLAGS | strip_flags)"
cxxflags="$(mk KFX_CXXFLAGS | strip_flags)"
tomlflags="$(mk TOML_CFLAGS | strip_flags)"
cc="$(mk CC)"; cxx="$(mk CXX)"
listed="$(mk KFX_SOURCES; mk TOML_SOURCES; echo deps/glad/src/glad.c)"

work="$(mktemp -d)"
# shellcheck disable=SC2317  # called through trap
cleanup() {
    git worktree remove --force "$work/raw" >/dev/null 2>&1 || true
    git worktree remove --force "$work/filtered" >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT
git worktree add -q --detach "$work/raw" "$commit"
git worktree add -q --detach "$work/filtered" "$commit"
"$LINUX_ONLY_DIR/filter-tree.sh" "$mode" --keep-lines "$work/filtered" >/dev/null

# Generated headers and downloaded dependencies are not in git: share the
# checkout's copies with both trees, so both see the same bytes.
for t in raw filtered; do
    for d in deps/centijson deps/astronomy deps/enet6 deps/libcurl; do
        rm -rf "${work:?}/${t:?}/${d:?}"; ln -s "$repo/$d" "$work/$t/$d"
    done
    cp src/ver_defs.h "$work/$t/src/ver_defs.h"
done

{
    tr ' ' '\n' <<<"$listed"
    if [ "$mode" = --upstream ]; then
        git -C "$work/raw" ls-files -- 'src/*.c' 'src/*.cpp' ':!src/ftests/*'
    fi
} | grep -v '^$' | sort -u > "$work/files"

# preprocess <tree> <file>: the preprocessor output, or exit 1.
cat > "$work/pp.sh" <<EOF
#!/usr/bin/env bash
tree="\$1" f="\$2" out="\${3:-}"   # read before "set --" below replaces them
case "\$f" in
    deps/centitoml/*) set -- $cc $tomlflags ;;
    *.c)              set -- $cc $cflags ;;
    *)                set -- $cxx $cxxflags ;;
esac
cd "$work/\$tree" && SOURCE_DATE_EPOCH=0 "\$@" -fno-working-directory -E -o - "\$f" 2>/dev/null
EOF
chmod +x "$work/pp.sh"
# obj.sh <tree> <file> <out.o>: compile without debug info. Used only when the
# preprocessed text differs (a dropped #include of an unused declaration does
# that): identical machine code is then the proof.
# shellcheck disable=SC2016  # \$ is literal: it edits the generated script
sed -e 's/-E -o - "\$f"/-g0 -c -o "\$out" "\$f"/' "$work/pp.sh" > "$work/obj.sh"
chmod +x "$work/obj.sh"

# shellcheck disable=SC2317  # called through xargs
compare_one() {  # prints one status line per file
    local f="$1" a b
    if [ ! -f "$work/raw/$f" ]; then echo "SKIP-ABSENT $f"; return; fi
    if [ ! -f "$work/filtered/$f" ]; then echo "REMOVED $f"; return; fi
    a="$work/out/raw/$f.i"; b="$work/out/filtered/$f.i"
    mkdir -p "$(dirname "$a")" "$(dirname "$b")"
    local ra=0 rb=0
    "$work/pp.sh" raw "$f" > "$a" || ra=$?
    "$work/pp.sh" filtered "$f" > "$b" || rb=$?
    if [ "$ra" != 0 ] && [ "$rb" != 0 ]; then echo "NOT-LINUX $f"; return; fi
    if [ "$ra" != 0 ]; then echo "ONLY-FILTERED-BUILDS $f"; return; fi
    if [ "$rb" != 0 ]; then echo "BROKEN $f"; return; fi
    if cmp -s "$a" "$b"; then echo "SAME $f"; return; fi
    if "$work/obj.sh" raw "$f" "$a.o" && "$work/obj.sh" filtered "$f" "$b.o" \
        && cmp -s "$a.o" "$b.o"; then
        echo "SAME-CODE $f"
    else
        echo "DIFFERENT $f"
    fi
}
export -f compare_one
export work
# shellcheck disable=SC2016  # $1 expands in the child bash
xargs -a "$work/files" -P "$(nproc)" -I{} bash -c 'compare_one "$1"' _ {} > "$work/results"

tr ' ' '\n' <<<"$listed" | grep -v '^$' | sort -u > "$work/listed"
status_files() { grep "^$1 " "$work/results" | cut -d' ' -f2 | sort || true; }

same=$(status_files SAME | wc -l)
samecode="$(status_files SAME-CODE)"
echo "linux-only: $same file(s) preprocess identically before and after the filter"
[ -z "$samecode" ] || echo "linux-only: $(wc -l <<<"$samecode") file(s) differ only in declarations and compile to identical object code: $(paste -sd' ' <<<"$samecode")"
removed="$(status_files REMOVED)"
[ -z "$removed" ] || echo "linux-only: removed by the filter (Windows-only): $(paste -sd' ' <<<"$removed")"
notlinux="$(status_files NOT-LINUX)"
[ -z "$notlinux" ] || echo "linux-only: do not build on Linux before or after: $(paste -sd' ' <<<"$notlinux")"

fail=0
bad="$(grep -E '^(DIFFERENT|BROKEN) ' "$work/results" || true)"
if [ -n "$bad" ]; then
    fail=1
    echo "linux-only: THE FILTER CHANGED WHAT THE COMPILER SEES:" >&2
    printf '    %s\n' "${bad//$'\n'/$'\n'    }" >&2
    for f in $(status_files DIFFERENT | head -3); do
        diff -u "$work/out/raw/$f.i" "$work/out/filtered/$f.i" | head -40 >&2 || true
    done
fi
# Something linux.mk compiles must neither be removed nor fail to build.
needed="$( { comm -12 <(printf '%s\n' "$removed" | grep -v '^$' || true) "$work/listed";
             comm -12 <(printf '%s\n' "$notlinux" | grep -v '^$' || true) "$work/listed"; } )"
if [ -n "$needed" ]; then
    fail=1
    echo "linux-only: linux.mk compiles file(s) that the filter removes or that do not build:" >&2
    printf '    %s\n' "${needed//$'\n'/$'\n'    }" >&2
fi
[ "$fail" = 0 ] && echo "linux-only: PROVEN -- the filter does not change what the Linux build compiles."
exit "$fail"
