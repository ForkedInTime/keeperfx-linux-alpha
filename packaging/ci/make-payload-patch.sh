#!/usr/bin/env bash
# Build an update patch: the files that differ between a previously released
# payload and the one just assembled.
#
# Why this exists: the full payload is ~390MB, and almost none of it moves between
# releases. Measured across two real releases (1.4.0.5425 -> 1.4.0.5488), 40 of
# 5674 files changed -- and 32 of the 33MB was the engine and launcher binaries.
# Everything else, the campaigns and sounds and graphics, was byte-identical.
# Sending all of it every time costs the player a 390MB download to receive what
# is usually two executables.
#
#   make-payload-patch.sh <new-payload-dir> <base-payload.7z> <out.7z>
#
# The patch is applied by extracting it over an existing install, exactly as the
# full payload is, so it needs no new machinery on the receiving side.
#
# DELETIONS are deliberately not expressed here, because extracting an archive
# cannot remove anything. They are already handled: the launcher reads
# launcher-auto-file-removal.txt from the game directory and deletes what it
# lists. That file lives in the payload, so whenever a release retires files the
# list changes, the patch carries the new list as a changed file, and the launcher
# acts on it. The check at the end proves that held for this pair rather than
# assuming it.
set -uo pipefail

NEW_DIR=${1:?usage: make-payload-patch.sh <new-payload-dir> <base-payload.7z> <out.7z>}
BASE_7Z=${2:?missing base payload archive}
OUT_7Z=${3:?missing output archive path}

[ -d "$NEW_DIR" ]  || { echo "::error::payload directory not found: $NEW_DIR" >&2; exit 1; }
[ -f "$BASE_7Z" ]  || { echo "::error::base payload not found: $BASE_7Z" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
BASE_DIR="$WORK/base"
mkdir -p "$BASE_DIR"

echo "unpacking base payload..."
7z x -bso0 -bsp0 -y "$BASE_7Z" -o"$BASE_DIR" >/dev/null || {
    echo "::error::could not unpack the base payload" >&2; exit 1; }

# Files in the new payload that the base does not have, or has differently.
# Compared by content: a rebuild changes timestamps on everything, so anything
# based on mtime would call the whole payload changed and produce a patch the
# size of the payload.
CHANGED="$WORK/changed.txt"
: > "$CHANGED"
( cd "$NEW_DIR" && find . -type f -printf '%P\n' ) | LC_ALL=C sort > "$WORK/new-files.txt"
( cd "$BASE_DIR" && find . -type f -printf '%P\n' ) | LC_ALL=C sort > "$WORK/base-files.txt"

while IFS= read -r rel; do
    if [ ! -f "$BASE_DIR/$rel" ] || ! cmp -s "$NEW_DIR/$rel" "$BASE_DIR/$rel"; then
        printf '%s\n' "$rel" >> "$CHANGED"
    fi
done < "$WORK/new-files.txt"

n_new=$(wc -l < "$WORK/new-files.txt")
n_changed=$(wc -l < "$CHANGED")

# Files the base had and the new payload does not. Reported, never packed --
# see the note at the top about how removals actually reach the player.
GONE="$WORK/gone.txt"
LC_ALL=C comm -23 "$WORK/base-files.txt" "$WORK/new-files.txt" > "$GONE"
n_gone=$(wc -l < "$GONE")

echo "payload files:      $n_new"
echo "changed or new:     $n_changed"
echo "present only in base: $n_gone"

if [ "$n_changed" -eq 0 ]; then
    # Nothing to send. Emitting an empty archive would advertise an update that
    # changes nothing, so emit no asset at all and let the caller notice.
    echo "::warning::no files differ from the base payload; no patch written"
    exit 2
fi

# Every file the payload has dropped must be named in the removal list, or a
# patched install keeps a file a fresh install would not have.
#
# The test is membership, NOT whether the list changed. The launcher re-reads
# launcher-auto-file-removal.txt on every run and deletes whatever it names, so a
# file retired several releases ago is still cleaned up by a list that has not
# been touched since. Requiring the list to have changed rejected a perfectly
# correct pair (1.4.0.5425 -> 1.4.0.5488 drops 555 files, all of them already
# listed, by a list identical in both payloads).
if [ "$n_gone" -gt 0 ]; then
    REMOVAL_LIST="$NEW_DIR/launcher-auto-file-removal.txt"
    if [ ! -f "$REMOVAL_LIST" ]; then
        echo "::error::$n_gone file(s) dropped from the payload but there is no launcher-auto-file-removal.txt to list them"
        exit 1
    fi
    # The list is grouped into "[1.4.0.5273]" version sections, with one path per
    # line beneath each. Comments start '#', and paths may be written with or
    # without a leading slash. Drop the section headers as well as the comments --
    # left in, each header became a phantom "listed file" that matches nothing.
    # Harmless as it happens, since a bogus extra entry can only fail to match, but
    # it made the listed-set 16 lines longer than the truth.
    grep -vE '^[[:space:]]*(#|\[)' "$REMOVAL_LIST" | sed 's|^/||' | sed 's|[[:space:]]*$||' \
      | grep -v '^$' | LC_ALL=C sort -u > "$WORK/removal-list.txt"
    LC_ALL=C comm -23 "$GONE" "$WORK/removal-list.txt" > "$WORK/unlisted.txt"
    n_unlisted=$(wc -l < "$WORK/unlisted.txt")
    if [ "$n_unlisted" -gt 0 ]; then
        # Fatal, because the alternative is an install that quietly differs from a
        # fresh one and nobody finds out for months.
        #
        # A file dropped from the payload but not named in the list is never deleted
        # from a player's directory, so an updated install keeps it and a fresh
        # install does not. That is not always harmless: multiplayer/classic/
        # map00123.{apt,lgt,tng} were dropped while map00123 itself is still shipped,
        # and .apt, .lgt and .tng are all read by the engine -- so the two installs
        # loaded different action points, lights and things for the same multiplayer
        # map. Players in one game, starting from different state.
        #
        # This was a warning while the shipped list was stale: the payload carried an
        # old copy of launcher-auto-file-removal.txt, 59 entries behind the
        # repository's, so genuinely-listed files looked unlisted. That is fixed
        # where it belongs -- the payload now ships the same list the prune uses --
        # which leaves this check meaning what it says. If it fires now, a file was
        # retired and nobody said what should happen to the copies already out there.
        echo "::error::$n_unlisted file(s) dropped from the payload are not named in launcher-auto-file-removal.txt."
        echo "::error::Updated installs would keep them while fresh installs would not. Add them to the list,"
        echo "::error::or keep shipping them. Do not leave the two kinds of install disagreeing."
        sed 's/^/  unlisted: /' "$WORK/unlisted.txt" >&2
        exit 1
    fi
    echo "dropped files:      $n_gone ($((n_gone - n_unlisted)) named in the removal list)"
fi

rm -f "$OUT_7Z"
# -mx=9 because this is downloaded far more often than it is built, and the
# archive is small enough that the extra compression time is irrelevant.
( cd "$NEW_DIR" && 7z a -bso0 -bsp0 -mx=9 "$OUT_7Z" -i@"$CHANGED" ) >/dev/null || {
    echo "::error::could not create the patch archive" >&2; exit 1; }

[ -f "$OUT_7Z" ] || { echo "::error::patch archive was not created" >&2; exit 1; }

patch_bytes=$(stat -c%s "$OUT_7Z")
base_bytes=$(stat -c%s "$BASE_7Z")
echo "patch:  $(numfmt --to=iec "$patch_bytes")  ($n_changed files)"
echo "full:   $(numfmt --to=iec "$base_bytes")"

# A patch bigger than the thing it replaces is worse than useless: it costs a
# download and still needs the full payload's guarantees. Say so loudly.
if [ "$patch_bytes" -ge "$base_bytes" ]; then
    echo "::warning::patch is not smaller than the full payload; the full download is the better choice here"
fi
exit 0
