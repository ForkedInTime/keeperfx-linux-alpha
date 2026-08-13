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

    # The AUR keys the repository on pkgbase, which for a split package is not
    # every pkgname -- check pkgbase, not pkgname.
    grep -q "^pkgbase = ${pkg}\$" "$WORK/$pkg/.SRCINFO" || {
        echo "    .SRCINFO pkgbase does not match '$pkg'" >&2; return 1; }

    cd "$WORK/$pkg"
    git add -A
    if git diff --cached --quiet; then
        echo "    already up to date"
        return 0
    fi
    # pkgrel belongs in the message: a rebuild against a bumped soname changes
    # pkgrel and nothing else, and "Update to 1.4.0.5425" on an unchanged version
    # tells an AUR user nothing about why the package moved.
    local msg="Update to $(sed -n 's/^\tpkgver = //p' .SRCINFO)-$(sed -n 's/^\tpkgrel = //p' .SRCINFO)"
    git rev-parse HEAD >/dev/null 2>&1 || msg="Initial import: $desc"
    git -c user.name="$NAME" -c user.email="$EMAIL" commit -q -m "$msg"
    git push -q origin master
    echo "    pushed $(git rev-parse --short HEAD): $msg"
    cd - >/dev/null
}

rc=0
# One repository: the split package serves keeperfx-tux, keeperfx-tux-data and
# keeperfx-tux-launcher. Every local file named in the PKGBUILD's source=() must
# be listed here -- the AUR repository is the whole build context, so one missing
# launcher script means the package fails to build for everyone who installs it.
import_pkg keeperfx-tux "$HERE" "KeeperFX Tux Edition" \
           PKGBUILD .SRCINFO keeperfx-tux.sh keeperfx-tux.desktop keeperfx-tux-launcher.sh \
           keeperfx-tux.hook keeperfx-tux-libcheck.sh || rc=1

if [ "$rc" = 0 ]; then
    echo
    echo "keeperfx-tux is on the AUR, serving both it and keeperfx-tux-data."
    echo "From here publish-aur.yml keeps it current on every release."
fi
exit "$rc"
