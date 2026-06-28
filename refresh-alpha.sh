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
make -f linux.mk BUILD_NUMBER=0 VER_SUFFIX=alpha -j"$(nproc)"

echo "==> installing engine + config into $PREFIX (binaries/saves untouched)"
[ -d "$PREFIX" ] || { echo "install dir $PREFIX missing — run a first install first"; exit 1; }
cp -f bin/keeperfx "$PREFIX/keeperfx"
# config/TEXT data tracks the engine version (sound config, creatures, campaigns)
for d in fxdata creatrs mods; do mkdir -p "$PREFIX/$d"; cp -rf "config/$d/." "$PREFIX/$d/" 2>/dev/null || true; done
for d in campgns levels lang; do [ -d "$d" ] && { mkdir -p "$PREFIX/$d"; cp -rf "$d/." "$PREFIX/$d/" 2>/dev/null || true; }; done

echo "==> done. Launch:  keeperfx-alpha"
