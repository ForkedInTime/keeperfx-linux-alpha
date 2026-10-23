#!/usr/bin/env bash
# Refuses to let a save-format change ship unnoticed.
#
# Since upstream #5424 a saved game describes its own layout: every field is
# stored under its name, with its type and size, and read back by name, so a
# build can load saves written by earlier ones as long as the format's tables
# change compatibly. What the tables say is packaging/ci/save-format/ (written
# by upstream's tools/save-tool), and that is what this compares; the history
# is in packaging/ci/save-format-baseline, which a failure sends people to read.
#
# Three modes:
#
#   check-save-format.sh
#       Build kfx-savetool for THIS tree and compare its schema, migrations and
#       format version with packaging/ci/save-format/. Also checks that the
#       tables list every member of every saved struct (check-save-tables.sh):
#       a member they miss is simply not saved. Exit 1 on any difference, with
#       upstream's verdict (tools/save-tool/policy.py) on whether older saves
#       still load.
#
#   check-save-format.sh --update
#       Rewrite packaging/ci/save-format/ from this tree. Do it in the commit
#       that changes the format, and say in its message what the policy check
#       said.
#
#   check-save-format.sh --release-note <git-ref>
#       Print the markdown warning for a release body IF saves written by
#       <git-ref> (the previous release's tag) will not load in this tree.
#       Prints nothing and exits 0 when they will; exits 3 when <git-ref> has
#       no baseline at all, i.e. the answer is not knowable rather than "no".
#       Reads git history and runs policy.py; it builds nothing.
#
# Run it by hand exactly as CI does, from the repository root:
#     packaging/ci/check-save-format.sh
set -euo pipefail

cd "$(dirname "$0")/../.."

BASELINE_DIR="packaging/ci/save-format"
HISTORY_FILE="packaging/ci/save-format-baseline"
POLICY="tools/save-tool/policy.py"

# The three files policy.py reads from each side: schema.txt, migrations.txt,
# and save_codec.h for SAVE_MIN_READABLE_MAJOR (only the version defines are
# kept here).
write_side() {   # write_side <dir>: what this tree's tables say, into <dir>
    local dir="$1" tool
    mkdir -p "$dir"
    tool="$(mktemp -d)/kfx-savetool"
    # Always rebuilt: a stale tool would describe the previous tree, which is
    # the one failure this check must never have. SDL_INCLUDES must not be
    # empty, or build.sh downloads the headers instead of using these.
    SDL_INCLUDES="$(pkg-config --cflags-only-I sdl3 2>/dev/null) -Isrc" \
        bash tools/save-tool/build.sh "$tool" >/dev/null
    "$tool" schema --build > "$dir/schema.txt"
    "$tool" migrations > "$dir/migrations.txt"
    grep -E '^#define[[:space:]]+SAVE_(FORMAT_MAJOR|FORMAT_MINOR|MIN_READABLE_MAJOR)[[:space:]]' \
        src/kfx/save/core/save_codec.h > "$dir/save_codec.h"
    rm -rf "$(dirname "$tool")"
}

policy() {   # policy <base dir> <head dir> <out.json>
    python3 "$POLICY" --base "$1" --head "$2" --pr 0 --head-sha HEAD --out "$3" >/dev/null
}

has_break() {   # has_break <result.json>
    python3 -c 'import json,sys; sys.exit(0 if any(f["kind"] == "break" for f in json.load(open(sys.argv[1]))["findings"]) else 1)' "$1"
}

print_findings() {   # print_findings <result.json>
    python3 -c '
import json, sys
findings = json.load(open(sys.argv[1]))["findings"]
if not findings:
    print("  (none: the policy check sees nothing that needs doing)")
for f in findings:
    print("  - %s: %s" % (f["kind"], ", ".join(f["args"])))' "$1"
}

# ---------------------------------------------------------------- release note
if [ "${1:-}" = "--release-note" ]; then
    prev_ref="${2:-}"
    if [ -z "$prev_ref" ]; then
        echo "usage: $0 --release-note <git-ref>" >&2
        exit 2
    fi
    work="$(mktemp -d)"
    trap 'rm -rf "$work"' EXIT

    if ! git show "${prev_ref}:${BASELINE_DIR}/schema.txt" > /dev/null 2>&1; then
        if ! git show "${prev_ref}:${HISTORY_FILE}" > /dev/null 2>&1; then
            # Older than any save-format guard, or the tag is gone. Saying
            # nothing is the safe answer: a wrong "your saves are dead" banner
            # on a release that did not break anything costs more trust than a
            # missing one.
            echo "no save-format baseline at ${prev_ref}; cannot tell whether its saves load" >&2
            exit 3
        fi
        # <git-ref> wrote raw struct Game dumps, which no build with the
        # self-describing format reads. This is the one break the new format
        # costs, and it says so.
        cat <<EOF
<!-- save-format-guard -->
> [!WARNING]
> **Saved games from earlier versions cannot be loaded by this release — for the last time.**
>
> Saved games used to be a direct copy of the engine's memory, so any change to
> it made every older save unreadable. From this version on a saved game
> describes its own layout and is read field by field, so saves made with this
> release keep loading in later ones. Saves from earlier versions are kept on
> disk and listed greyed out; they still open in the version that wrote them.
>
> Campaign progress is not affected: Continue, completed levels, bonus levels
> and the creatures carried to the next level all carry over, as do high scores.
> Finish a level in progress on your current version before updating, or replay
> it from its start.
EOF
        exit 0
    fi

    mkdir -p "$work/prev"
    for f in schema.txt migrations.txt save_codec.h; do
        git show "${prev_ref}:${BASELINE_DIR}/${f}" > "$work/prev/$f"
    done
    policy "$work/prev" "$BASELINE_DIR" "$work/result.json"
    if ! has_break "$work/result.json"; then
        exit 0
    fi
    cat <<EOF
<!-- save-format-guard -->
> [!WARNING]
> **Saved games from earlier versions cannot be loaded by this release.**
>
> This build raised the save format's major version, the oldest version it can
> read, or dropped a migration, so saves written before it will be refused.
> Campaign progress is kept. Finish a level in progress on your current version
> before updating, or keep the old build alongside this one.
EOF
    exit 0
fi

update=0
if [ "${1:-}" = "--update" ]; then
    update=1
    shift
fi
if [ $# -gt 0 ]; then
    echo "usage: $0 [--update | --release-note <git-ref>]" >&2
    exit 2
fi

# ---------------------------------------------------------------- tables
packaging/ci/check-save-tables.sh

if [ "$update" = 1 ]; then
    write_side "$BASELINE_DIR"
    echo "save-format check: ${BASELINE_DIR} rewritten from this tree."
    git --no-pager diff --stat -- "$BASELINE_DIR" || true
    exit 0
fi

# ---------------------------------------------------------------- check
if [ ! -f "${BASELINE_DIR}/schema.txt" ]; then
    echo "save-format check: ${BASELINE_DIR}/schema.txt is missing -- the guard cannot run." >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
write_side "$work/now"

if diff -q "${BASELINE_DIR}/schema.txt" "$work/now/schema.txt" > /dev/null \
   && diff -q "${BASELINE_DIR}/migrations.txt" "$work/now/migrations.txt" > /dev/null \
   && diff -q "${BASELINE_DIR}/save_codec.h" "$work/now/save_codec.h" > /dev/null; then
    echo "save-format check: OK -- $(head -n 1 "$work/now/schema.txt"), $(($(wc -l < "$work/now/schema.txt") - 1)) saved fields, matching ${BASELINE_DIR}."
    exit 0
fi

policy "$BASELINE_DIR" "$work/now" "$work/result.json"
{
    echo
    echo "================================================================"
    echo " SAVE FORMAT CHANGED"
    echo "================================================================"
    echo
    echo "The save format's tables in this tree differ from ${BASELINE_DIR}:"
    echo
    # diff exits 1 when the files differ, which they do here; under pipefail
    # that would end the script before the policy verdict below.
    diff -u "${BASELINE_DIR}/schema.txt" "$work/now/schema.txt" | sed -n '3,60p' | sed 's/^/    /' || true
    diff -u "${BASELINE_DIR}/save_codec.h" "$work/now/save_codec.h" | sed -n '3,20p' | sed 's/^/    /' || true
    echo
    echo "Upstream's policy check (${POLICY}) on the change:"
    print_findings "$work/result.json"
    echo
    if has_break "$work/result.json"; then
        echo "  This BREAKS older saves: no build after it will load a save written"
        echo "  before it. Say so in the commit message and CHANGELOG.md; the release"
        echo "  notes get the warning automatically once the baseline changes."
    else
        echo "  Older saves keep loading: a field the tables gain gets its default"
        echo "  from them, one they lose is skipped. Check any finding above (a"
        echo "  missing alias drops a renamed field's value)."
    fi
    echo
    echo "WHAT TO DO"
    echo
    echo "  Accept it deliberately, in the same commit as the change that caused"
    echo "  it, by running"
    echo
    echo "      packaging/ci/check-save-format.sh --update"
    echo
    echo "  and saying in the commit message what changed and what the policy"
    echo "  check said. Read ${HISTORY_FILE} first."
    echo "================================================================"
} >&2
exit 1
