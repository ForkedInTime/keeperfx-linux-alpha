#!/usr/bin/env bash
#
# Generate the engine's UTF-8 fonts (fxdata/font12.fxfont, font16.fxfont and the
# _JPN/_CHT variants), needed since upstream #4920, from tools/fxfontmaker.
# linux.mk does not build them; the team's own CI generates them the same way.
#
#   packaging/ci/make-fonts.sh        -> tools/fxfontmaker/*.fxfont
#
# One copy of these steps instead of five (the release workflows, the AUR
# PKGBUILD and refresh-alpha.sh each carried their own, all with the unifont
# version hard-coded). The source files are found by pattern, so a new unifont
# release from the team needs no edit here -- and if one ever does not match,
# this stops with a message instead of producing a game that draws no text.
# The weekly sync runs it too, so that shows up on the sync PR, not at release.
set -euo pipefail
cd "$(dirname "$0")/../../tools/fxfontmaker"
PY="$(command -v python3 || command -v python)" || { echo "make-fonts: python3 is required" >&2; exit 1; }

# exactly_one <description> <glob>: the single file matching glob, or stop.
exactly_one() {
    local desc="$1" pattern="$2" matches
    # shellcheck disable=SC2206  # the glob is the point
    matches=($pattern)
    if [ "${#matches[@]}" -ne 1 ] || [ ! -f "${matches[0]}" ]; then
        echo "make-fonts: expected exactly one $desc ($pattern) in tools/fxfontmaker, found: ${matches[*]}" >&2
        exit 1
    fi
    printf '%s\n' "${matches[0]}"
}
unifont="$(exactly_one "Unifont" 'unifont-[0-9]*.hex')"
unifont_jp="$(exactly_one "Japanese Unifont" 'unifont_jp-[0-9]*.hex')"
unifont_t="$(exactly_one "Chinese (traditional) Unifont" 'unifont_t-[0-9]*.hex')"
wenquanyi="$(exactly_one "WenQuanYi bitmap font" 'wenquanyi_*.bdf')"

trap 'rm -f merged12.hex wenquanyi.hex unifont12.hex' EXIT
"$PY" rescale_unifont_hex.py "$unifont" unifont12.hex
"$PY" bdf_to_hex.py "$wenquanyi" wenquanyi.hex
"$PY" merge_hex.py unifont12.hex wenquanyi.hex merged12.hex
"$PY" unifont_hex_to_binary.py "$unifont"    font16.fxfont     16
"$PY" unifont_hex_to_binary.py "$unifont_jp" font16_JPN.fxfont 16
"$PY" unifont_hex_to_binary.py "$unifont_t"  font16_CHT.fxfont 16
"$PY" unifont_hex_to_binary.py merged12.hex  font12.fxfont     12
for f in font12.fxfont font16.fxfont font16_JPN.fxfont font16_CHT.fxfont; do
    [ -s "$f" ] || { echo "make-fonts: $f was not produced" >&2; exit 1; }
done
fonts=(./*.fxfont)
echo "make-fonts: ${#fonts[@]} fonts from $unifont, $unifont_jp, $unifont_t and $wenquanyi"
