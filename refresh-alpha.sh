#!/usr/bin/env bash
# Rebuild the personal KeeperFX alpha from the current checkout and install it.
# Run from this repo's root. Override PREFIX to install somewhere else.
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local/share/keeperfx-alpha}"

echo "==> fetching upstream (the KeeperFX team's master)"
git fetch upstream

echo "==> what the team changed since our base:"
git log --oneline HEAD..upstream/master | head -60 || true

# This script was written when the fork was a handful of fixes carried on top of
# the team's master, so refreshing meant rebasing onto it. That is no longer what
# this repository is: sync is merge-based (the weekly bot opens a merge PR, and
# those are merged, never squashed), and the fork's implementation wins where the
# two disagree -- a rebase inverts that and replays our commits onto their tree.
#
# It is also actively destructive right now. Upstream migrated to SDL3 (#5085) on
# 2026-08-05. That migration is ported and verified, but parked on
# sync/upstream-2026-08-05-sdl3 until the release workflows build somewhere that
# ships SDL3 -- ubuntu-24.04 does not. Rebasing here would drag every fork commit
# onto the migration we deliberately have not taken.
#
# Left behind an opt-in rather than deleted, because the reconciliation it does is
# still occasionally the right tool -- just never the default.
if [ "${REBASE_ONTO_UPSTREAM:-0}" = "1" ]; then
  echo "==> rebasing our fixes onto their latest master (REBASE_ONTO_UPSTREAM=1)"
  if ! git rebase upstream/master; then
    cat <<MSG
!! Rebase hit conflicts. The team touched a file our fixes change
   (bflib_video.c / linux.mk / LensManager.cpp / main.cpp). Resolve them, then:
       git rebase --continue
   and re-run this script.
MSG
    exit 1
  fi
else
  echo "==> not rebasing onto upstream; building this checkout as it stands"
  echo "    (sync is merge-based -- see the note in this script)"
fi

echo "==> building (fetch curl-downloaded deps serially first to avoid a -j race)"
make -f linux.mk deps/centijson/include/json.h deps/astronomy/include/astronomy.h \
                 deps/enet6/include/enet6/enet.h deps/libcurl/lib/libcurl.a
# Build number = git commit count, exactly as the team's CI computes it
# (build-alpha-patch-unsigned.yml: BUILD_NUMBER=$(git rev-list --count HEAD)).
# Version then reads "<major>.<minor>.<release>.<count> alpha", with the first three
# taken from version.mk (upstream controls them; 1.4.0 at the time of writing), and
# a count high enough that keeperfx-launcher-qt enables every version-gated setting.
BUILD_NUMBER=$(git rev-list --count HEAD)
echo "    BUILD_NUMBER=$BUILD_NUMBER"
# ver_defs.h only regenerates when version.mk changes, so force it — otherwise a
# stale build number (e.g. 0) is silently reused and the launcher disables settings.
rm -f src/ver_defs.h
make -f linux.mk BUILD_NUMBER="$BUILD_NUMBER" VER_SUFFIX=alpha -j"$(nproc)"

echo "==> generating UTF-8 unifont .fxfont files (needed since #4920; not built by linux.mk)"
# The engine loads fxdata/font12.fxfont + font16.fxfont (+ _JPN/_CHT variants) for
# text rendering. The team's CI generates them from tools/fxfontmaker; replicate that.
( cd tools/fxfontmaker && PY=$(command -v python3 || command -v python) && \
  "$PY" rescale_unifont_hex.py unifont-17.0.04.hex unifont12.hex && \
  "$PY" bdf_to_hex.py wenquanyi_9pt.bdf wenquanyi.hex && \
  "$PY" merge_hex.py unifont12.hex wenquanyi.hex merged12.hex && \
  "$PY" unifont_hex_to_binary.py unifont-17.0.04.hex    font16.fxfont     16 && \
  "$PY" unifont_hex_to_binary.py unifont_jp-17.0.04.hex font16_JPN.fxfont 16 && \
  "$PY" unifont_hex_to_binary.py unifont_t-17.0.04.hex  font16_CHT.fxfont 16 && \
  "$PY" unifont_hex_to_binary.py merged12.hex           font12.fxfont     12 && \
  rm -f merged12.hex wenquanyi.hex unifont12.hex )

echo "==> installing engine + config into $PREFIX (binaries/saves untouched)"
[ -d "$PREFIX" ] || { echo "install dir $PREFIX missing — run a first install first"; exit 1; }
# If the keeperfx-tux package has been installed, its wrapper links the packaged
# engine into this directory from a read-only /usr prefix, and the copy below
# either fails outright or would clobber a packaged file. That install belongs to
# pacman, not to this script.
if [ -L "$PREFIX/keeperfx" ]; then
  cat <<MSG
!! $PREFIX/keeperfx is a symlink to $(readlink "$PREFIX/keeperfx")
   That game directory is managed by the keeperfx-tux package, not by this script.
   To install a dev build without disturbing it, pick another prefix:
       PREFIX=~/kfx-dev ./refresh-alpha.sh
MSG
  exit 1
fi
cp -f bin/keeperfx "$PREFIX/keeperfx"
# Record the engine's version where keeperfx-launcher-qt can read it: a native ELF
# has no Windows PE ProductVersion resource, so the launcher reads version.txt instead.
sed -n 's/.*VER_STRING  "\(.*\)".*/\1/p' src/ver_defs.h > "$PREFIX/version.txt"
echo "    version: $(cat "$PREFIX/version.txt")"
# config/TEXT data tracks the engine version (sound config, creatures, campaigns)
for d in fxdata creatrs mods; do mkdir -p "$PREFIX/$d"; cp -rf "config/$d/." "$PREFIX/$d/" 2>/dev/null || true; done
for d in campgns levels lang multiplayer; do [ -d "$d" ] && { mkdir -p "$PREFIX/$d"; cp -rf "$d/." "$PREFIX/$d/" 2>/dev/null || true; }; done
# the generated unifont binaries live alongside the text config in fxdata/
cp -f tools/fxfontmaker/*.fxfont "$PREFIX/fxdata/" 2>/dev/null || true

echo "==> done. Launch:  keeperfx-alpha"
