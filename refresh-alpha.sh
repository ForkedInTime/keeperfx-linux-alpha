#!/usr/bin/env bash
# Update the personal KeeperFX alpha: pull the KeeperFX team's latest master,
# rebase our fixes onto it, rebuild, and install. Run from this repo's root.
set -euo pipefail

PREFIX="$HOME/.local/share/keeperfx-alpha"

echo "==> fetching upstream (the KeeperFX team's master)"
git fetch upstream

echo "==> what the team changed since our base:"
git log --oneline HEAD~1..upstream/master | head -60 || true

echo "==> rebasing our alpha fixes onto their latest master"
if ! git rebase upstream/master; then
  cat <<MSG
!! Rebase hit conflicts. The team touched a file our fixes change
   (bflib_video.c / linux.mk / LensManager.cpp / main.cpp). Resolve them, then:
       git rebase --continue
   and re-run this script. (This is the expected ~yearly reconciliation.)
MSG
  exit 1
fi

echo "==> building (fetch curl-downloaded deps serially first to avoid a -j race)"
make -f linux.mk deps/centijson/include/json.h deps/astronomy/include/astronomy.h \
                 deps/enet6/include/enet6/enet.h deps/libcurl/lib/libcurl.a
# Build number = git commit count, exactly as the team's CI computes it
# (build-alpha-patch-unsigned.yml: BUILD_NUMBER=$(git rev-list --count HEAD)).
# Version then reads "1.3.2.<count> alpha" — the team's real alpha line, and high
# enough that the keeperfx-launcher-qt enables every version-gated setting.
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
cp -f bin/keeperfx "$PREFIX/keeperfx"
# Record the engine's version where keeperfx-launcher-qt can read it: a native ELF
# has no Windows PE ProductVersion resource, so the launcher reads version.txt instead.
sed -n 's/.*VER_STRING  "\(.*\)".*/\1/p' src/ver_defs.h > "$PREFIX/version.txt"
echo "    version: $(cat "$PREFIX/version.txt")"
# config/TEXT data tracks the engine version (sound config, creatures, campaigns)
for d in fxdata creatrs mods; do mkdir -p "$PREFIX/$d"; cp -rf "config/$d/." "$PREFIX/$d/" 2>/dev/null || true; done
for d in campgns levels lang; do [ -d "$d" ] && { mkdir -p "$PREFIX/$d"; cp -rf "$d/." "$PREFIX/$d/" 2>/dev/null || true; }; done
# the generated unifont binaries live alongside the text config in fxdata/
cp -f tools/fxfontmaker/*.fxfont "$PREFIX/fxdata/" 2>/dev/null || true

echo "==> done. Launch:  keeperfx-alpha"
