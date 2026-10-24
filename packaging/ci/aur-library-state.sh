#!/usr/bin/env bash
# Prints the library ABI the AUR package is built against, as Arch's repositories have it
# right now. Compared week by week (aur-library-watch.yml), a change means every Arch
# install of the stable is about to break: pacman refuses the library upgrade, so
# `yay -Syu` stops, until the package is rebuilt (publish-aur.yml, rebuild).
#
#   aur-library-state.sh <PKGBUILD>              the state, one line per library
#   aur-library-state.sh --unpinned <PKGBUILD>   the packages that must be installed first
#
# Give it the recipe that is LIVE on the AUR (what users build), not this repository's:
# alpha's can list different libraries from the stable's.
#
# Two kinds of dependency, both read from the engine's depends= in the PKGBUILD:
#  - sonames ('libavformat.so'): the version range the providing package declares, which
#    is what makepkg writes into the built package and what pacman then holds to;
#  - packages that publish no soname ('sdl3'): the SONAME their shared objects carry. Those
#    are read from the installed files, so the packages have to be installed.
# Needs an up-to-date Arch system (pacman -Sy) and readelf.
set -euo pipefail

unpinned_only=0
if [ "${1:-}" = "--unpinned" ]; then
    unpinned_only=1
    shift
fi
pkgbuild="${1:?usage: aur-library-state.sh [--unpinned] <PKGBUILD>}"

# The quoted words of package_keeperfx-tux()'s depends=( ... ).
words=$(awk '/^package_keeperfx-tux\(\)/ { f = 1 } f && /depends=\(/ { d = 1 } d { print } d && /^  \)/ { exit }' "$pkgbuild" \
    | sed 's/#.*//' | grep -oE "'[^']+'" | tr -d "'")
[ -n "$words" ] || { echo "aur-library-state: no depends= found in $pkgbuild" >&2; exit 1; }
sonames=$(printf '%s\n' "$words" | grep -E '\.so$' || true)
# Everything else except what is always there or is ours.
unpinned=$(printf '%s\n' "$words" | grep -vE '\.so$' | grep -vxE 'glibc|gcc-libs|bash|hicolor-icon-theme' || true)

if [ "$unpinned_only" = 1 ]; then
    printf '%s\n' "$unpinned"
    exit 0
fi

{
for so in $sonames; do
    provider=$(pacman -Sp --print-format '%n' "$so" 2>/dev/null | tail -n1)
    [ -n "$provider" ] || { echo "aur-library-state: nothing in the repositories provides $so" >&2; exit 1; }
    range=$(pacman -Si "$provider" | sed -n 's/^Provides *: //p' | tr -s ' ' '\n' | grep -E "^${so//./\\.}=" || true)
    printf '%s %s\n' "$provider" "${range:-$so (no version)}"
done
for pkg in $unpinned; do
    pacman -Qq "$pkg" >/dev/null 2>&1 || { echo "aur-library-state: $pkg is not installed" >&2; exit 1; }
    for f in $(pacman -Qlq "$pkg" | grep -E '^/usr/lib/[^/]+\.so(\.[0-9]+)*$'); do
        [ -L "$f" ] && continue
        soname=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')
        [ -n "$soname" ] && printf '%s %s\n' "$pkg" "$soname"
    done
done
} | LC_ALL=C sort -u
