#!/usr/bin/env bash
# Rebuild the personal KeeperFX alpha from the current checkout and install it.
# Run from this repo's root. Override PREFIX to install somewhere else.
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local/share/keeperfx-alpha}"

echo "==> fetching upstream (the KeeperFX team's master)"
git fetch upstream

echo "==> what the team changed since our last sync:"
# Not HEAD..upstream/master: the sync merges Linux-only snapshots of the team's
# tree, so their commits are not in our history (packaging/linux-only/README.md).
git log --oneline "$(packaging/linux-only/synced-upstream.sh)..upstream/master" | head -60 || true

# This script used to offer REBASE_ONTO_UPSTREAM=1, a rebase of the fork onto the
# team's master. It is gone: the sync merges Linux-only snapshots of their tree
# (packaging/linux-only/README.md), and a rebase onto their raw master would
# bring back every Windows-only file and #ifdef the filter removed -- on top of
# inverting which side wins where the fork and the team disagree. To take the
# team's latest by hand, merge a snapshot instead:
#     git merge "$(packaging/linux-only/make-snapshot.sh upstream/master)"
echo "==> building this checkout as it stands (to sync, merge a snapshot -- see above)"

echo "==> building (fetch curl-downloaded deps serially first to avoid a -j race)"
make -f linux.mk deps/centijson/include/json.h deps/astronomy/include/astronomy.h \
                 deps/enet6/include/enet6/enet.h deps/libcurl/lib/libcurl.a
# Build number = the team's numbering: git commit count, plus the team's commits
# that arrived through Linux-only snapshots (packaging/linux-only/build-number.sh).
# Version then reads "<major>.<minor>.<release>.<count> alpha", with the first three
# taken from version.mk (upstream controls them; 1.4.0 at the time of writing), and
# a count high enough that keeperfx-launcher-qt enables every version-gated setting.
BUILD_NUMBER=$(packaging/linux-only/build-number.sh)
echo "    BUILD_NUMBER=$BUILD_NUMBER"
# ver_defs.h only regenerates when version.mk changes, so force it — otherwise a
# stale build number (e.g. 0) is silently reused and the launcher disables settings.
rm -f src/ver_defs.h
make -f linux.mk BUILD_NUMBER="$BUILD_NUMBER" VER_SUFFIX=alpha -j"$(nproc)"

echo "==> generating UTF-8 unifont .fxfont files (needed since #4920; not built by linux.mk)"
# The engine loads fxdata/font12.fxfont + font16.fxfont (+ _JPN/_CHT variants) for
# text rendering. The team's CI generates them from tools/fxfontmaker; replicate that.
packaging/ci/make-fonts.sh

echo "==> installing engine + config into $PREFIX (binaries/saves untouched)"

# This script UPDATES an existing install; it cannot create one, and it must not
# pretend otherwise. The repository carries configuration, not content:
#
#   * no maps -- zero .lif files are tracked (verified: `git ls-files '*.lif'`
#     is empty). campgns/, levels/ and multiplayer/ hold the .cfg files that
#     DESCRIBE map packs, while the maps themselves ship in the data payload.
#   * no generated language data -- config/fxdata/*.dat and *.fon are build
#     outputs of pkg_lang.mk, correctly gitignored.
#   * no ldata/ at all -- GUI strings and the intro movies live in the payload.
#
# Copying the repo trees over an empty directory therefore yields an install
# that either dies at startup with "Setting up game failed" (no GUI strings) or
# starts with an empty Free Play list and no multiplayer map packs. Both look
# like engine bugs and are not, which is exactly why this check is worth the
# lines: a plain `[ -d ]` test passes for `mkdir newdir` and the breakage only
# surfaces later, in-game.
missing=""
[ -d "$PREFIX" ] || missing="$missing\n   - the directory itself does not exist"
[ -e "$PREFIX/ldata" ] || missing="$missing\n   - ldata/        (GUI strings + intro movies; without it the game will not start)"
ls "$PREFIX"/fxdata/gtext_*.dat >/dev/null 2>&1 || \
  missing="$missing\n   - fxdata/gtext_*.dat  (generated language data; without it the game will not start)"
[ -e "$PREFIX/data" ] || missing="$missing\n   - data/         (your own original Dungeon Keeper files)"
if [ -d "$PREFIX" ] && [ "$(find -L "$PREFIX" -iname '*.lif' 2>/dev/null | head -1)" = "" ]; then
  missing="$missing\n   - any .lif maps (Free Play and the multiplayer map packs would be empty)"
fi
if [ -n "$missing" ] && [ -n "${SEED_FROM:-}" ]; then
  # Seeding makes a complete install out of an incomplete one by taking the
  # content the repository cannot supply from a game directory that already has
  # it. --ignore-existing (or cp -n) is the important part: anything this script
  # is about to install fresh must win, so seeding only ever FILLS GAPS.
  [ -d "$SEED_FROM" ] || { echo "!! SEED_FROM=$SEED_FROM is not a directory"; exit 1; }
  [ -e "$SEED_FROM/ldata" ] || { echo "!! SEED_FROM=$SEED_FROM is not a complete install either (no ldata/)"; exit 1; }
  echo "==> seeding $PREFIX from $SEED_FROM (filling gaps only)"
  mkdir -p "$PREFIX"
  if command -v rsync >/dev/null; then
    # -L resolves symlinks into packaged trees, so the result does not depend on
    # the seed install (or its distro package) still being there afterwards.
    rsync -aL --ignore-existing \
      --exclude 'keeperfx' --exclude 'keeperfx.log*' --exclude '*.bak-*' \
      --exclude '*.orig-*' --exclude '*.tmp' --exclude 'keeperfx-launcher-qt*' \
      "$SEED_FROM/" "$PREFIX/"
  else
    echo "   (rsync not found; falling back to cp -a, which is slower)"
    for e in "$SEED_FROM"/*; do
      b=$(basename "$e")
      case "$b" in keeperfx|keeperfx.log*|*.bak-*|*.orig-*|*.tmp|keeperfx-launcher-qt*) continue;; esac
      [ -e "$PREFIX/$b" ] || cp -aL "$e" "$PREFIX/$b"
    done
  fi
  # Re-run the same checks against the seeded directory rather than assuming the
  # copy fixed everything.
  missing=""
  [ -e "$PREFIX/ldata" ] || missing="$missing\n   - ldata/"
  ls "$PREFIX"/fxdata/gtext_*.dat >/dev/null 2>&1 || missing="$missing\n   - fxdata/gtext_*.dat"
  [ -e "$PREFIX/data" ] || missing="$missing\n   - data/"
  [ "$(find -L "$PREFIX" -iname '*.lif' 2>/dev/null | head -1)" = "" ] && missing="$missing\n   - any .lif maps"
  if [ -n "$missing" ]; then
    printf '!! seeding from %s did not produce a complete install; still missing:%b\n' "$SEED_FROM" "$missing"
    exit 1
  fi
  echo "    seeded: $(find -L "$PREFIX" -iname '*.lif' 2>/dev/null | wc -l) maps, $(ls "$PREFIX"/fxdata/gtext_*.dat 2>/dev/null | wc -l) language files"
fi

if [ -n "$missing" ]; then
  printf '!! %s does not look like a complete KeeperFX install.\n' "$PREFIX"
  printf '   Missing:%b\n' "$missing"
  cat <<'MSG'

   The maps, ldata/ and generated language data ship in the data payload and are
   not in git, so this repository alone cannot produce a playable install. Point
   SEED_FROM at a game directory that already has them and this script will fill
   the gaps for you:

       SEED_FROM=~/.local/share/keeperfx-alpha PREFIX=~/kfx-dev ./refresh-alpha.sh

   Any existing install works as a seed -- the AppImage's, the packaged one, or
   another dev copy. Nothing in the seed is modified.
MSG
  # Name a candidate rather than making them go looking for one.
  for cand in "$HOME/.local/share/keeperfx-alpha" "$HOME/.local/share/keeperfx-tux-alpha"; do
    if [ "$cand" != "$PREFIX" ] && [ -e "$cand/ldata" ]; then
      echo "   Found a usable seed on this machine: $cand"
      break
    fi
  done
  exit 1
fi
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
