#!/usr/bin/env bash
#
# Fail if a KeeperFX work tree contains anything the Linux-only filter removes.
#
#   check-tree.sh [dir]      (default: the repository this script lives in)
#
# Checks the fork's tree, so the [upstream-only] section of remove.list (the
# team's .github/, which the fork replaces with its own) is not applied. It
# reports:
#   - tracked files matching a [windows] entry of remove.list;
#   - C/C++ files where unifdef would still delete a Windows-only branch;
#   - #if/#ifdef/#elif lines that still name a symbol from windows-macros.txt.
#
# Run by the weekly sync after every merge and by the linux-only guard on pull
# requests, so Windows content cannot come back by a manual merge either.
# Exit status: 0 clean, 1 Windows content found, 2 error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

dir="${1:-$LINUX_ONLY_DIR/../..}"
require_git_worktree "$dir"
command -v unifdef >/dev/null || die "unifdef is not installed (apt/pacman: unifdef)"
cd "$dir"

found=0
report() { found=1; printf '  %s\n' "$@"; }

mapfile -t specs < <(pathspecs windows)
mapfile -t files < <(git ls-files -- "${specs[@]}")
if [ "${#files[@]}" -gt 0 ]; then
    echo "Windows-only files (packaging/linux-only/remove.list):"
    report "${files[@]}"
fi

mapfile -t uargs < <(unifdef_args)
regex="$(macro_regex)"
mapfile -d '' -t candidates < <(git ls-files -z -- "${C_LIKE_PATHSPECS[@]}" \
    | xargs -0 -r grep -lZ -E "$regex" -- 2>/dev/null || true)
code=()
for f in "${candidates[@]}"; do
    [ -f "$f" ] || continue
    rc=0
    unifdef "${uargs[@]}" "$f" >/dev/null 2>&1 || rc=$?
    case "$rc" in
        0) ;;
        1) code+=("$f") ;;
        *) die "unifdef could not parse $f" ;;
    esac
done
if [ "${#code[@]}" -gt 0 ]; then
    echo "Windows-only code under #if (run packaging/linux-only/filter-tree.sh --fork .):"
    report "${code[@]}"
fi

leftover="$(git ls-files -z -- "${C_LIKE_PATHSPECS[@]}" \
    | xargs -0 -r grep -nE "^[[:space:]]*#[[:space:]]*(el)?if.*$regex" -- 2>/dev/null || true)"
if [ -n "$leftover" ]; then
    echo "Preprocessor conditions that still name a Windows symbol:"
    mapfile -t lines <<<"$leftover"
    report "${lines[@]}"
fi

if [ "$found" = 0 ]; then
    echo "linux-only: clean -- no Windows-only files or code."
    exit 0
fi
exit 1
