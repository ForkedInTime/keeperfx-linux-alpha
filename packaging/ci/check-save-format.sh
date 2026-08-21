#!/usr/bin/env bash
# Refuses to let a save-format break ship unnoticed.
#
# A saved game is a raw dump of `struct Game`, and the engine's only
# compatibility check is `hdr.len != sizeof(struct Game)` (src/game_saves.c).
# So that number is the save format version, and any field added anywhere
# inside it invalidates every save in existence. packaging/ci/save-format-baseline
# carries the full story and is what a failure sends people to read.
#
# Two modes:
#
#   check-save-format.sh
#       Measure sizeof(struct Game) for THIS tree and compare it with the
#       tracked baseline. Prints the size it measured. Exit 1 on a mismatch,
#       with an explanation of what breaks and how to accept it deliberately.
#
#   check-save-format.sh --release-note <git-ref>
#       Print the markdown warning for a release body IF the baseline differs
#       from the one at <git-ref> (the previous release's tag). Prints nothing
#       and exits 0 when the format is unchanged; exits 3 when <git-ref>
#       predates the baseline file, i.e. the answer is not knowable rather than
#       "no". Reads git history only -- it does not build anything.
#
# Run it by hand exactly as CI does, from the repository root:
#     packaging/ci/check-save-format.sh
set -euo pipefail

cd "$(dirname "$0")/../.."

BASELINE_FILE="packaging/ci/save-format-baseline"
PROBE="bin/save-format-probe"
KEY="sizeof_struct_game"

read_baseline() {   # read_baseline <file-contents-on-stdin>
    sed -n "s/^${KEY}=\([0-9][0-9]*\).*/\1/p" | tail -n 1
}

# ---------------------------------------------------------------- release note
if [ "${1:-}" = "--release-note" ]; then
    prev_ref="${2:-}"
    if [ -z "$prev_ref" ]; then
        echo "usage: $0 --release-note <git-ref>" >&2
        exit 2
    fi

    now="$(read_baseline < "$BASELINE_FILE")"
    if ! prev_blob="$(git show "${prev_ref}:${BASELINE_FILE}" 2>/dev/null)"; then
        # The previous release predates this guard, or its tag is gone. Saying
        # nothing is the safe answer: a wrong "your saves are dead" banner on a
        # release that did not break anything costs more trust than a missing one.
        echo "no ${BASELINE_FILE} at ${prev_ref}; cannot tell whether the save format changed" >&2
        exit 3
    fi
    prev="$(printf '%s\n' "$prev_blob" | read_baseline)"

    if [ -z "$prev" ] || [ "$prev" = "$now" ]; then
        exit 0
    fi

    cat <<EOF
<!-- save-format-guard -->
> [!WARNING]
> **Saved games from earlier versions cannot be loaded by this release.**
>
> A change in this build altered the layout of the in-memory game state that
> saved games are a direct copy of. The engine accepts a save only when its
> size matches the running build exactly, so every save written before this
> version — including autosaves and the "Continue" slot — will be refused.
> Finish anything in progress on your current version before updating, or keep
> the old build alongside this one.
>
> Saves made from this version on stay loadable until this warning appears again.
> (Game state size: ${prev} → ${now} bytes.)
EOF
    exit 0
fi

if [ $# -gt 0 ]; then
    echo "usage: $0 [--release-note <git-ref>]" >&2
    exit 2
fi

# ---------------------------------------------------------------------- check
if [ ! -f "$BASELINE_FILE" ]; then
    echo "save-format check: ${BASELINE_FILE} is missing -- the guard cannot run." >&2
    exit 1
fi

expected="$(read_baseline < "$BASELINE_FILE")"
if [ -z "$expected" ]; then
    echo "save-format check: no ${KEY}=<number> line in ${BASELINE_FILE}." >&2
    exit 1
fi

# Always rebuild. The probe's make rule lists no header prerequisites (the
# engine's do not either), so a stale binary from a previous checkout would
# happily report the previous tree's size -- which is the one failure this
# guard must never have.
rm -f "$PROBE"
make -f linux.mk "$PROBE" >/dev/null

measured_all="$("./$PROBE")"
measured="$(printf '%s\n' "$measured_all" | read_baseline)"
thing_size="$(printf '%s\n' "$measured_all" | sed -n 's/^sizeof_struct_thing=\([0-9]*\).*/\1/p')"
things_count="$(printf '%s\n' "$measured_all" | sed -n 's/^things_count=\([0-9]*\).*/\1/p')"

if [ "$measured" = "$expected" ]; then
    echo "save-format check: OK -- sizeof(struct Game) = ${measured} bytes, matching ${BASELINE_FILE}."
    echo "  (struct Thing = ${thing_size} bytes x ${things_count} things)"
    exit 0
fi

delta=$(( measured - expected ))
{
    echo
    echo "================================================================"
    echo " SAVE FORMAT CHANGED -- EVERY EXISTING SAVED GAME WILL BE LOST"
    echo "================================================================"
    echo
    echo "  sizeof(struct Game) expected : ${expected} bytes  (${BASELINE_FILE})"
    echo "  sizeof(struct Game) measured : ${measured} bytes  (this working tree)"
    echo "  difference                   : ${delta} bytes"
    echo
    echo "A saved game is a raw dump of struct Game, and the engine loads one"
    echo "only if its length equals sizeof(struct Game) for the running build"
    echo "(src/game_saves.c). Something in this tree changed that size, so this"
    echo "build CANNOT LOAD ANY SAVE written by any earlier build -- not the"
    echo "player's campaigns, not their autosaves, not Continue."
    echo
    if [ -n "$things_count" ] && [ "$things_count" -gt 0 ] \
       && [ $(( delta % things_count )) -eq 0 ]; then
        echo "  Likely cause: struct Thing grew by $(( delta / things_count )) bytes."
        echo "  It is currently ${thing_size} bytes and struct Game holds ${things_count} of them,"
        echo "  so even one added field is multiplied ${things_count}x. That is exactly how"
        echo "  this bit us on 2026-08-19."
        echo
    fi
    echo "WHAT TO DO"
    echo
    echo "  Not intended?  Do not touch the baseline. Fix the change: keep the"
    echo "                 new state out of struct Game, or reuse space that is"
    echo "                 already there. Most fields that trip this only matter"
    echo "                 at runtime and never needed saving."
    echo
    echo "  Intended?      Accept it deliberately -- in the same commit as the"
    echo "                 change that caused it -- by editing"
    echo "                 ${BASELINE_FILE}:"
    echo
    echo "                     ${KEY}=${measured}"
    echo
    echo "                 and saying in the commit message and CHANGELOG.md"
    echo "                 that saves from earlier versions will not load. The"
    echo "                 release notes get that warning automatically once the"
    echo "                 baseline changes."
    echo
    echo "Read ${BASELINE_FILE} before deciding. This check is"
    echo "never made green by editing it without one of the two answers above."
    echo "================================================================"
} >&2
exit 1
