#!/bin/sh
# Builds the engine with AddressSanitizer + UBSan and runs every installed
# campaign's first level under it for a fixed window. The deliverable is the
# report: every out-of-bounds access, use-after-free and piece of undefined
# behaviour the pass exercises, with stack traces, whether or not it would
# have crashed a normal build.
#
# Needs a real install (game data + original DK files), so this runs on a dev
# machine, not in CI -- CI has no rights to the original game files. CI keeps
# the SANITIZE=1 build itself from bit-rotting (see sanitize-build.yml).
#
#   tests/sanitize-regression.sh [seconds-per-campaign]   (default 20)
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GAMEDIR="${KEEPERFX_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/keeperfx-alpha}"
SECS="${1:-20}"
OUT="$ROOT_DIR/bin/sanitize-reports"

[ -d "$GAMEDIR/campgns" ] || { echo "no game install at $GAMEDIR" >&2; exit 1; }

echo "== clean sanitized build (objects are shared with normal builds) =="
make -f "$ROOT_DIR/linux.mk" -C "$ROOT_DIR" clean >/dev/null
make -f "$ROOT_DIR/linux.mk" -C "$ROOT_DIR" SANITIZE=1 -j"$(nproc)" >/dev/null || {
    echo "sanitized build FAILED" >&2; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT"
ERR="$ROOT_DIR/bin/sanitize-stderr.txt"
ln -sfn "$ROOT_DIR/bin/keeperfx" "$GAMEDIR/keeperfx-sanitize"
trap 'rm -f "$GAMEDIR/keeperfx-sanitize" "$ERR"' EXIT

# detect_leaks=0: a 1997 game leaks at exit by design; leak noise would bury
# the findings that matter. halt_on_error=0 on UBSan so one benign wrap does
# not end the run -- everything is logged and counted instead.
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=0:log_path=$OUT/asan"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:log_path=$OUT/ubsan"
# Silent: the sound code still runs, through SDL's and OpenAL's null drivers, but
# a 16-campaign pass no longer plays through the speakers of whoever runs it.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" ALSOFT_DRIVERS="${ALSOFT_DRIVERS:-null}"

printf '%-12s %-6s %-8s %s\n' CAMPAIGN LEVEL VERDICT REPORTS
fail=0
for cfg in "$GAMEDIR"/campgns/*.cfg; do
    name=$(basename "$cfg" .cfg)
    # Campaigns number their levels independently (keeporig starts at 1,
    # revlord at 1234); ask each cfg for its own first level.
    level=$(grep -iE "^SINGLE_LEVELS" "$cfg" | sed 's/.*=//' | awk '{print $1}')
    [ -n "$level" ] || level=1

    before=$(ls "$OUT" 2>/dev/null | wc -l)
    ( cd "$GAMEDIR" && timeout "$SECS" ./keeperfx-sanitize \
        -campaign "$name" -level "$level" -nointro >/dev/null 2>"$ERR" )
    rc=$?
    # In an ASan build UBSan ignores log_path (GCC 16): its "runtime error:"
    # reports go to stderr, so stderr is kept whenever it carries one.
    grep -q "runtime error:" "$ERR" && mv "$ERR" "$OUT/ubsan-stderr.$name"
    after=$(ls "$OUT" 2>/dev/null | wc -l)

    # 124 is timeout ending a still-running game: the pass case. ASan aborts
    # exit with its own code (1 by default) after writing its log.
    case "$rc" in
        124) verdict=PASS ;;
        *)   verdict="EXIT$rc"; fail=1 ;;
    esac
    started=$(grep -c "Started level" "$GAMEDIR/keeperfx.log" 2>/dev/null || true)
    [ "${started:-0}" -eq 0 ] && { verdict=NOSTART; fail=1; }
    printf '%-12s %-6s %-8s %s\n' "$name" "$level" "$verdict" "$((after - before)) new report file(s)"
done

echo
echo "== summary of sanitizer findings =="
if ls "$OUT"/* >/dev/null 2>&1; then
    # One line per distinct finding site, with a count -- the triage list.
    # UBSan lines become "file:line: kind", their values folded to N, or every
    # shifted value gets a line. Misaligned accesses (the game's packed 1997
    # structs: thousands of sites, harmless on x86-64) fold to one line per
    # struct type; the full reports keep their sites.
    findings=$(grep -hE "SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer)|runtime error:" "$OUT"/* \
        | awk -F': runtime error: ' 'NF == 2 { loc = $1; sub(/:[0-9]+$/, "", loc); msg = $2;
              if (msg ~ /misaligned address/) { sub(/.*for type /, "", msg); sub(/, which requires.*/, "", msg);
                  print "misaligned access to a packed " msg; next }
              gsub(/-?[0-9]+/, "N", msg); print loc ": " msg; next } { print }' \
        | sort | uniq -c | sort -rn)
    # Everything else first: the misaligned lines are the long, known tail.
    printf '%s\n' "$findings" | grep -v "misaligned access to a packed"
    printf '%s\n' "$findings" | grep "misaligned access to a packed"
    echo "full reports: $OUT/"
else
    echo "no sanitizer reports -- the pass ran clean"
fi
exit $fail
