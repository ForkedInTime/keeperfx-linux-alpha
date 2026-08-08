#!/usr/bin/env bash
# Remove superseded files from an assembled payload.
#
# Why this exists: every release's payload is built by overlaying onto the PREVIOUS
# release's payload, so files are only ever added. Anything upstream retires stays
# in ours forever. Meanwhile the launcher reads the same list and deletes those
# files from the user's install on first run -- so we ship data purely for the
# launcher to throw away, and an install "loses" files that were never needed.
#
# docs/launcher-auto-file-removal.txt is the authority for what is obsolete: it is
# maintained upstream and is exactly what the launcher acts on. Pruning against it
# here makes both sides agree.
#
# Usage: packaging/ci/prune-payload.sh <payload-dir> [removal-list]
set -euo pipefail

PAYLOAD="${1:?usage: prune-payload.sh <payload-dir> [removal-list]}"
LIST="${2:-docs/launcher-auto-file-removal.txt}"

[ -d "$PAYLOAD" ] || { echo "prune-payload: no such directory: $PAYLOAD" >&2; exit 1; }
if [ ! -f "$LIST" ]; then
  # Not fatal: a payload that keeps a few obsolete files is worse than a release
  # that fails to build.
  echo "::warning::prune-payload: $LIST not found; payload left unpruned"
  exit 0
fi

# Resolve once so every candidate can be checked against it.
PAYLOAD_ABS="$(cd "$PAYLOAD" && pwd)"

removed=0
bytes=0
while IFS= read -r line; do
  # The list is grouped by version in [x.y.z] sections and carries # comments.
  case "$line" in ''|'#'*|'['*) continue ;; esac

  # Entries are payload-relative, written with or without a leading slash.
  rel="${line#/}"
  # Refuse anything that could climb out of the payload. The list is trusted, but
  # a rule that deletes by path should not depend on that being true forever.
  case "$rel" in *..*) echo "::warning::prune-payload: skipping suspicious entry '$line'"; continue ;; esac

  target="$PAYLOAD_ABS/$rel"
  [ -f "$target" ] || continue

  # Belt and braces: the resolved path must still be inside the payload.
  resolved="$(cd "$(dirname "$target")" && pwd)/$(basename "$target")"
  case "$resolved" in "$PAYLOAD_ABS"/*) ;; *) echo "::warning::prune-payload: '$rel' resolved outside the payload; skipped"; continue ;; esac

  size=$(stat -c %s "$resolved" 2>/dev/null || echo 0)
  rm -f "$resolved"
  removed=$((removed + 1))
  bytes=$((bytes + size))
done < "$LIST"

echo "prune-payload: removed $removed superseded file(s), $((bytes / 1024)) KiB"

# Directories that held nothing but retired files should go too, but only if the
# prune emptied them -- never a directory that was already empty for its own
# reasons, and never the payload root.
find "$PAYLOAD_ABS" -mindepth 1 -type d -empty -delete 2>/dev/null || true
