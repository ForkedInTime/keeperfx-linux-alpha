#!/usr/bin/env bash
# Launcher shim for the packaged KeeperFX Tux Edition.
#
# The engine roots every path it touches at the directory of argv[0] (see
# process_command_line() in src/main.cpp), and it writes save games, screenshots,
# keeperfx.cfg and keeperfx.log into that same directory. A read-only /usr prefix
# therefore cannot be the runtime directory. This shim assembles a per-user game
# directory that links back to the package payload, then execs the engine through
# a path inside it so the engine roots itself there.
set -euo pipefail

PREFIX=/usr/share/keeperfx-linux-alpha
GAMEDIR="${KEEPERFX_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/keeperfx-alpha}"

# Read-only, versioned with the package: replaced on every launch so that a
# pacman upgrade actually reaches the user's game directory.
RO_DIRS=(fxdata creatrs campgns levels lang)
# User-owned: seeded once, never clobbered afterwards.
RW_DIRS=(mods music)
# Supplied by the user, from their own Dungeon Keeper installation.
DATA_DIRS=(data sound)

mkdir -p "$GAMEDIR"/{save,scrshots}

for d in "${RO_DIRS[@]}"; do
    [ -e "$PREFIX/$d" ] || continue
    # Only replace our own symlink; never delete a real directory the user made.
    if [ -L "$GAMEDIR/$d" ] || [ ! -e "$GAMEDIR/$d" ]; then
        ln -sfn "$PREFIX/$d" "$GAMEDIR/$d"
    fi
done

for d in "${RW_DIRS[@]}"; do
    [ -e "$PREFIX/$d" ] || continue
    mkdir -p "$GAMEDIR/$d"
    cp -rn "$PREFIX/$d/." "$GAMEDIR/$d/" 2>/dev/null || true
done

ln -sfn "$PREFIX/keeperfx" "$GAMEDIR/keeperfx"
[ -e "$PREFIX/version.txt" ] && cp -f "$PREFIX/version.txt" "$GAMEDIR/version.txt"

missing=()
for d in "${DATA_DIRS[@]}"; do
    # A populated directory is required; an empty one counts as missing.
    if [ ! -d "$GAMEDIR/$d" ] || [ -z "$(ls -A "$GAMEDIR/$d" 2>/dev/null)" ]; then
        missing+=("$d")
    fi
done

if [ ${#missing[@]} -gt 0 ]; then
    cat >&2 <<EOF
KeeperFX cannot start: the game data is missing.

  Game directory: $GAMEDIR
  Missing:        ${missing[*]}

This package ships the engine, campaigns and configuration. It cannot ship the
Dungeon Keeper data files, which you must supply from your own copy of the game
(GOG, Steam or the original CD). Any one of these will populate them:

  1. Install the Qt launcher and let it fetch and assemble the data tree:
       https://github.com/ForkedInTime/keeperfx-launcher-qt-linux

  2. Extract the full archive from a release into the game directory:
       7z x keeperfx-linux-alpha-x86_64-full.7z -o/tmp/kfx
       cp -rn /tmp/kfx/keeperfx-linux-alpha/. "$GAMEDIR/"

  3. Point at an existing KeeperFX or Dungeon Keeper installation:
       ln -s /path/to/install/data  "$GAMEDIR/data"
       ln -s /path/to/install/sound "$GAMEDIR/sound"

See /usr/share/doc/keeperfx-linux-alpha-git/README.md for the file list.
EOF
    exit 1
fi

# exec through the game directory so argv[0] — and therefore the engine's
# runtime directory — is $GAMEDIR rather than the read-only prefix.
cd "$GAMEDIR"
exec "$GAMEDIR/keeperfx" "$@"
