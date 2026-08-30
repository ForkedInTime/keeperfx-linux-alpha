#!/usr/bin/env bash
# Build 7-Zip's full-format shared library (7z.so) from source.
#
# Why this exists: the launcher extracts every archive through bit7z, which is a
# wrapper -- the actual formats come from whatever 7z.so sits beside the
# launcher binary. The library committed at launcher/ext/7z.so (unchanged since
# February 2025) carries the RAR *handler* but none of the RAR *decoders*:
# NArchive::NRar5 is present, NCompress::NRar5 is entirely absent. The visible
# effect is that a RAR workshop item lists its contents correctly and then dies
# part-way through extraction with "Unsupported method", after writing only the
# entries that happened to be stored uncompressed.
#
# That is not hypothetical: workshop item 414 ("Infernal Rift") is served as
# Rift.rar, and installing it failed for exactly this reason.
#
# 7-Zip's own Format7zF bundle is the full-format build -- it is what distros
# ship as 7z.so -- so build that rather than hunting for a binary to trust. The
# result is ~3.1MB against the old 2.4MB, and adds Rar/Rar2/Rar3/Rar5 decoding.
#
# LICENSING: 7-Zip is LGPL, EXCEPT the RAR decoder, which is under the unRAR
# licence: it may be redistributed, but must not be used to reconstruct the RAR
# *compression* algorithm. Every distro ships 7-Zip on these terms. The upstream
# License.txt is copied next to the library so the restriction travels with it.
#
# Usage:  packaging/ci/build-7zip-lib.sh [outdir]
#         packaging/ci/build-7zip-lib.sh --version        # the pinned version
#         packaging/ci/build-7zip-lib.sh --apt-packages   # what to apt-get first
set -euo pipefail

# Pinned so a rebuild is reproducible and a cache key can be derived from this
# script alone -- same reasoning as build-sdl3.sh.
SEVENZIP_VERSION="2602"
SEVENZIP_SHA256="cf967c98bca02a4b8b16375f441825a8e141362f14be1969bbec8e1ca0bff9dd"
SEVENZIP_URL="https://www.7-zip.org/a/7z${SEVENZIP_VERSION}-src.tar.xz"

if [ "${1:-}" = "--version" ]; then
    echo "$SEVENZIP_VERSION"
    exit 0
fi

# Nothing beyond a C++ toolchain; listed for symmetry with build-sdl3.sh so a
# workflow can ask both scripts what they need.
if [ "${1:-}" = "--apt-packages" ]; then
    echo "build-essential curl xz-utils"
    exit 0
fi

OUTDIR="${1:-$PWD/deps/7zip}"
mkdir -p "$OUTDIR"

# Already built (restored from cache): nothing to do.
if [ -f "$OUTDIR/7z.so" ]; then
    echo "7z.so already present in $OUTDIR; skipping build."
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Fetching 7-Zip ${SEVENZIP_VERSION} source..."
curl -fsSL -o "$WORK/7z-src.tar.xz" "$SEVENZIP_URL"

# Verify before unpacking: this library ends up in every user's install, and it
# is fetched over the network at build time from a host we do not control.
echo "${SEVENZIP_SHA256}  $WORK/7z-src.tar.xz" | sha256sum -c - \
    || { echo "7-Zip source checksum mismatch -- refusing to build" >&2; exit 1; }

mkdir -p "$WORK/src"
tar xf "$WORK/7z-src.tar.xz" -C "$WORK/src"

# -Werror is on by default in 7zip_gcc.mak and newer GCC finds warnings in this
# tree that its author's compiler did not (GCC 16 fails on Archive/*Handler.cpp).
# The warnings are upstream 7-Zip's to fix, not ours, and turning them fatal in
# our pipeline only means the library stops building whenever the runner's GCC
# advances. Keep -Wall, drop -Werror.
echo "Building Format7zF (full-format 7z.so)..."
make -C "$WORK/src/CPP/7zip/Bundles/Format7zF" \
     -f makefile.gcc -j"$(nproc)" \
     CFLAGS_WARN_WALL="-Wall -Wno-error" >/dev/null

BUILT="$WORK/src/CPP/7zip/Bundles/Format7zF/_o/7z.so"
[ -f "$BUILT" ] || { echo "build produced no 7z.so" >&2; exit 1; }

# Refuse to ship a library that lacks what this script exists to add. Without
# this the build would "succeed" and quietly reproduce the original bug.
#
# Counted rather than `strings ... | grep -q`: under `set -o pipefail` that form
# reports failure even on a match, because grep -q exits at the first hit and
# the still-writing strings then dies of SIGPIPE, which pipefail surfaces as the
# pipeline's status. It cost a real debugging detour here -- the guard rejected
# a library that did contain all 27 symbols.
rar_syms="$(strings "$BUILT" | grep -c "NCompress5NRar5" || true)"
if [ "${rar_syms:-0}" -eq 0 ]; then
    echo "built 7z.so has no NCompress::NRar5 decoder -- refusing to ship it" >&2
    exit 1
fi

install -Dm755 "$BUILT" "$OUTDIR/7z.so"
install -Dm644 "$WORK/src/DOC/License.txt" "$OUTDIR/7-zip-License.txt"

echo "Built $OUTDIR/7z.so ($(stat -c%s "$OUTDIR/7z.so") bytes), RAR decoding included."
