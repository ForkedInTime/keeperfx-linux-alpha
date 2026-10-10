#!/usr/bin/env bash
#
# Strip Windows-only content from a KeeperFX git work tree, in place.
#
#   filter-tree.sh --upstream <dir>    a snapshot of the team's tree (weekly sync)
#   filter-tree.sh --fork <dir>        this fork's own tree
#   add --keep-lines to blank removed lines instead of deleting them, so line
#   numbers survive (prove-equivalence.sh uses this; the sync does not)
#
# Two passes, both driven by files next to this script:
#
#   1. Paths. Everything listed in remove.list is `git rm`ed. --upstream also
#      applies the [upstream-only] section (the team's .github/); --fork leaves
#      those alone because the fork keeps its own content there.
#
#   2. Code. In every tracked C/C++ file that mentions one of the symbols in
#      windows-macros.txt, unifdef resolves each #if/#ifdef/#elif those symbols
#      decide as "not defined" -- which is what the Linux compiler sees anyway --
#      and deletes the branch that can never be compiled here. unifdef leaves a
#      condition alone when it also tests something unknown (`defined(__LP64__)
#      || defined(_WIN64)`), so simplify_conditions.py then substitutes 0 for the
#      Windows symbols in those, folds what that decides, and writes back either
#      the reduced condition or a marker unifdef resolves in a final pass.
#      Anything that still names a Windows symbol afterwards (syntax the
#      simplifier does not parse, such as __has_include) is reported and left
#      for a person; check-tree.sh fails on it.
#
# The result is staged in the work tree's index. Exit status: 0 on success,
# 2 on error (bad usage, unifdef failure), 3 if lines remain that name a
# Windows symbol and could not be reduced.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

mode="" keep_lines=0 dir=""
for arg in "$@"; do
    case "$arg" in
        --upstream|--fork) mode="${arg#--}" ;;
        --keep-lines) keep_lines=1 ;;
        -*) die "unknown option $arg" ;;
        *) [ -z "$dir" ] || die "one directory only"; dir="$arg" ;;
    esac
done
if [ -z "$mode" ] || [ -z "$dir" ]; then
    die "usage: $0 --upstream|--fork [--keep-lines] <git-work-tree>"
fi
require_git_worktree "$dir"
command -v unifdef >/dev/null || die "unifdef is not installed (apt/pacman: unifdef)"
command -v python3 >/dev/null || die "python3 is required"
cd "$dir"

# ---- 1. paths --------------------------------------------------------------
sections=(windows)
[ "$mode" = upstream ] && sections+=(upstream-only)
mapfile -t specs < <(pathspecs "${sections[@]}")
removed=0
if [ "${#specs[@]}" -gt 0 ]; then
    mapfile -d '' -t doomed < <(git ls-files -z -- "${specs[@]}")
    if [ "${#doomed[@]}" -gt 0 ]; then
        printf '%s\0' "${doomed[@]}" | xargs -0 git rm -q --
        removed=${#doomed[@]}
    fi
fi
echo "linux-only: removed $removed Windows-only file(s)"

# A removed header can still be #included by a file that stays, outside any
# #ifdef (PlatformManager.cpp includes PlatformWindows.h for a class it only
# uses under _WIN32). Drop those #include lines too. prove-equivalence.sh then
# shows the object code is unchanged -- if a removed header ever contributed
# something the Linux build needs, that proof fails.
if [ "$removed" -gt 0 ]; then
    includes=()
    for f in "${doomed[@]}"; do
        # Engine headers only, spelled the way the engine includes them:
        # relative to src/, linux.mk's -I root. Never by bare file name -- the
        # CUnit tree being removed has its own config.h, and matching names
        # alone would strip the engine's #include "config.h" everywhere. A
        # removed header included under any other spelling makes the filtered
        # tree fail to build, which prove-equivalence.sh reports.
        case "$f" in src/*.h|src/*.hpp|src/*.hh|src/*.hxx|src/*.inl) ;; *) continue ;; esac
        includes+=("${f#src/}")
    done
    if [ "${#includes[@]}" -gt 0 ]; then
        inc_re="$(printf '%s\n' "${includes[@]}" | sort -u | sed 's/[.[\*^$/]/\\&/g' | paste -sd'|')"
        mapfile -d '' -t includers < <(git ls-files -z -- "${C_LIKE_PATHSPECS[@]}" \
            | xargs -0 -r grep -lZ -E "^[[:space:]]*#[[:space:]]*include[[:space:]]*[\"<]($inc_re)[\">]" -- 2>/dev/null || true)
        for f in "${includers[@]}"; do
            if [ "$keep_lines" = 1 ]; then
                INC_RE="$inc_re" perl -i -pe 's/^\s*#\s*include\s*["<](?:$ENV{INC_RE})[">].*$//' "$f"
            else
                INC_RE="$inc_re" perl -i -ne 'print unless /^\s*#\s*include\s*["<](?:$ENV{INC_RE})[">]/' "$f"
            fi
        done
        [ "${#includers[@]}" = 0 ] || echo "linux-only: dropped #include of removed headers from ${#includers[@]} file(s)"
    fi
fi

# ---- 2. code ---------------------------------------------------------------
mapfile -t uargs < <(unifdef_args)
regex="$(macro_regex)"
ublank=()
[ "$keep_lines" = 1 ] && ublank=(-b)

mapfile -d '' -t candidates < <(git ls-files -z -- "${C_LIKE_PATHSPECS[@]}" \
    | xargs -0 -r grep -lZ -E "$regex" -- 2>/dev/null || true)

pykeep=()
[ "$keep_lines" = 1 ] && pykeep=(--keep-lines)
changed=0
for f in "${candidates[@]}"; do
    [ -f "$f" ] || continue
    before="$(sha1sum < "$f")"
    # -x2: exit 0 whether or not anything changed, 2 only on a real error.
    unifdef -m -x2 "${ublank[@]}" "${uargs[@]}" "$f" \
        || die "unifdef failed on $f"
    # Reduce conditions that mix a Windows symbol with other tests, then let
    # unifdef delete whatever that decided.
    python3 "$LINUX_ONLY_DIR/simplify_conditions.py" "${pykeep[@]}" "$MACROS_FILE" "$f" >/dev/null \
        || die "simplify_conditions.py failed on $f"
    unifdef -m -x2 "${ublank[@]}" "${uargs[@]}" -DLINUX_ONLY_TRUE -ULINUX_ONLY_FALSE "$f" \
        || die "unifdef failed on $f (second pass)"
    [ "$(sha1sum < "$f")" = "$before" ] || changed=$((changed + 1))
done
git add -u -- . >/dev/null
echo "linux-only: stripped Windows conditionals from $changed source file(s)"

# ---- leftovers ---------------------------------------------------------------
leftover="$(git ls-files -z -- "${C_LIKE_PATHSPECS[@]}" \
    | xargs -0 -r grep -nE "^[[:space:]]*#[[:space:]]*(el)?if.*($regex|LINUX_ONLY_(TRUE|FALSE))" -- 2>/dev/null || true)"
if [ -n "$leftover" ]; then
    echo "linux-only: these conditions still name a Windows symbol and need a person:" >&2
    printf '%s\n' "$leftover" | sed 's/^/    /' >&2
    exit 3
fi
