#!/usr/bin/env bash
# One-off import of both packages into the AUR.
#
# The AUR creates a package's git repository on first push, and CI cannot do that
# for a package that does not exist yet -- publish-aur.yml clones an existing repo
# and would fail. Run this once per package name; afterwards the workflow keeps
# both up to date on every release.
#
# Needs an SSH key registered on your AUR account. Verify with:
#   ssh aur@aur.archlinux.org        (should greet you by name)
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

NAME=$(git -C "$HERE/../.." config user.name  2>/dev/null || echo 'KeeperFX Tux Edition')
EMAIL=$(git -C "$HERE/../.." config user.email 2>/dev/null || echo 'noreply@users.noreply.github.com')

import_pkg() {
    local pkg=$1 src=$2 desc=$3; shift 3
    echo "==> $pkg"

    if ! git clone -q "ssh://aur@aur.archlinux.org/${pkg}.git" "$WORK/$pkg" 2>"$WORK/err"; then
        if grep -qi 'maintenance' "$WORK/err"; then
            echo "    the AUR is in maintenance; try again later" >&2
        else
            sed 's/^/    /' "$WORK/err" >&2
        fi
        return 1
    fi

    local f
    for f in "$@"; do cp "$src/$f" "$WORK/$pkg/"; done

    # The AUR rejects a push whose .SRCINFO disagrees with the repository name.
    grep -q "^pkgname = ${pkg}\$" "$WORK/$pkg/.SRCINFO" || {
        echo "    .SRCINFO pkgname does not match '$pkg'" >&2; return 1; }

    cd "$WORK/$pkg"
    git add -A
    if git diff --cached --quiet; then
        echo "    already up to date"
        return 0
    fi
    local msg="Update to $(sed -n 's/^\tpkgver = //p' .SRCINFO)"
    git rev-parse HEAD >/dev/null 2>&1 || msg="Initial import: $desc"
    git -c user.name="$NAME" -c user.email="$EMAIL" commit -q -m "$msg"
    git push -q origin master
    echo "    pushed $(git rev-parse --short HEAD): $msg"
    cd - >/dev/null
}

rc=0
import_pkg keeperfx-tux      "$HERE"      "KeeperFX Tux Edition engine"    \
           PKGBUILD .SRCINFO keeperfx-tux.sh keeperfx-tux.desktop || rc=1
import_pkg keeperfx-tux-data "$HERE/data" "KeeperFX Tux Edition game data" \
           PKGBUILD .SRCINFO || rc=1

if [ "$rc" = 0 ]; then
    echo
    echo "Both packages are on the AUR. From here publish-aur.yml keeps them"
    echo "current: it bumps both on every release and pushes."
fi
exit "$rc"
