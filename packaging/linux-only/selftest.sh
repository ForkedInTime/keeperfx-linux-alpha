#!/usr/bin/env bash
#
# Self-test for the Linux-only filter, on a small synthetic repository with
# known Windows content and an exact expected result. Run by the linux-only
# guard workflow on every pull request; run it after editing anything here.
#
#   packaging/linux-only/selftest.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fail=0
ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fail=1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# ---- simplify_conditions.py: one condition in, one expected line out --------
echo "simplify_conditions.py"
# Each line: <input>@@<expected> ("|" cannot separate them: the conditions use ||).
while IFS= read -r line; do
    [ -n "$line" ] || continue
    input="${line%%@@*}" expected="${line#*@@}"
    printf '%s\n' "$input" > "$work/c.h"
    python3 "$here/simplify_conditions.py" "$here/windows-macros.txt" "$work/c.h" >/dev/null
    got="$(cat "$work/c.h")"
    if [ "$got" = "$expected" ]; then ok "$input"; else bad "$input -> '$got' (want '$expected')"; fi
done <<'EOF'
#if defined(__LP64__) || defined(_WIN64) || defined(__aarch64__)@@#if defined(__LP64__) || defined(__aarch64__)
#if !defined(_WIN32) && defined(__unix__)@@#if defined(__unix__)
#if defined(_WIN32) || defined(__APPLE__) /* note */@@#if defined(__APPLE__) /* note */
#if !defined(_WIN32) || defined(X)@@#if defined(LINUX_ONLY_TRUE)
#elif A && defined(_WIN32) || B@@#elif B
#if (1 && _MSC_VER) + 2 > 1@@#if defined(LINUX_ONLY_TRUE)
#if !defined(SIMD) && ((defined(_MSC_VER) && _MSC_VER >= 1400) && defined(_M_X64)) || defined(__SSE2__)@@#if defined(__SSE2__)
#if (defined(_MSC_VER) || defined(SIMD)) && !defined(__clang__)@@#if defined(SIMD) && !defined(__clang__)
#if A - (B - C) && !_WIN32@@#if A - (B - C)
#if __has_include(<x.h>) || defined(_WIN32)@@#if __has_include(<x.h>) || defined(_WIN32)
#if 0xFFFFFFFFFFFFFFFFULL || defined(_WIN32)@@#if 0xFFFFFFFFFFFFFFFFULL || defined(_WIN32)
#if defined(__unix__)@@#if defined(__unix__)
EOF

# ---- filter-tree.sh + check-tree.sh on a synthetic repository ---------------
echo "filter-tree.sh / check-tree.sh"
repo="$work/repo"
mkdir -p "$repo/src/kfx/platform" "$repo/.github/workflows" "$repo/deps/CUnit-2.1-3" "$repo/res" "$repo/tools"
git -C "$repo" init -q
echo 'name: windows ci' > "$repo/.github/workflows/win.yml"
echo 'solution' > "$repo/keeperfx_vs2010.sln"
echo '@echo off' > "$repo/tools/make.bat"
echo 'icon' > "$repo/res/keeperfx_stdres.rc"
echo 'png' > "$repo/res/keeperfx_icon016-08bpp.png"
echo '/* cunit */' > "$repo/deps/CUnit-2.1-3/config.h"
printf 'class PlatformWindows {};\n' > "$repo/src/kfx/platform/PlatformWindows.h"
printf '#define CONFIG 1\n' > "$repo/src/config.h"
cat > "$repo/src/a.cpp" <<'EOF'
#include "config.h"
#include "kfx/platform/PlatformWindows.h"
#ifdef _WIN32
#include <windows.h>
int platform(void) { return 1; }
#else
int platform(void) { return 2; }
#endif
#if defined(__MINGW32__) && !defined(_GLIBCXX_HAS_GTHREADS)
int mingw;
#endif
#if defined(__LP64__) || defined(_WIN64)
int wide;
#endif
EOF
cat > "$work/expected.cpp" <<'EOF'
#include "config.h"
int platform(void) { return 2; }
#if defined(__LP64__)
int wide;
#endif
EOF
git -C "$repo" add -A
git -C "$repo" -c user.name=t -c user.email=t@t commit -q -m fixture

"$here/filter-tree.sh" --upstream "$repo" >/dev/null
for gone in .github/workflows/win.yml keeperfx_vs2010.sln tools/make.bat res/keeperfx_stdres.rc \
            deps/CUnit-2.1-3/config.h src/kfx/platform/PlatformWindows.h; do
    if [ -e "$repo/$gone" ]; then bad "$gone was not removed"; else ok "removed $gone"; fi
done
for kept in res/keeperfx_icon016-08bpp.png src/config.h; do
    if [ -e "$repo/$kept" ]; then ok "kept $kept"; else bad "$kept was removed"; fi
done
if diff -u "$work/expected.cpp" "$repo/src/a.cpp"; then
    ok "src/a.cpp reduced exactly (and #include \"config.h\" survived CUnit's config.h)"
else
    bad "src/a.cpp differs from the expected result (diff above)"
fi
if "$here/check-tree.sh" "$repo" >/dev/null; then ok "check-tree.sh: filtered tree is clean"; else bad "check-tree.sh rejects the filtered tree"; fi

printf '#ifdef _MSC_VER\nint msvc;\n#endif\n' > "$repo/src/b.c"
echo 'x' > "$repo/build.cmd"
git -C "$repo" add -A
if "$here/check-tree.sh" "$repo" >/dev/null; then bad "check-tree.sh missed a Windows file and #ifdef"; else ok "check-tree.sh catches a Windows file and #ifdef"; fi

# --fork must leave the fork's own .github alone.
git -C "$repo" -c user.name=t -c user.email=t@t commit -q -m more
mkdir -p "$repo/.github/workflows"; echo 'name: ours' > "$repo/.github/workflows/ours.yml"
git -C "$repo" add -A
"$here/filter-tree.sh" --fork "$repo" >/dev/null
if [ -e "$repo/.github/workflows/ours.yml" ]; then ok "--fork keeps the fork's .github/"; else bad "--fork removed .github/"; fi
if [ ! -e "$repo/build.cmd" ] && ! grep -q _MSC_VER "$repo/src/b.c"; then ok "--fork strips the fork's own Windows content"; else bad "--fork left Windows content"; fi

echo
if [ "$fail" = 0 ]; then echo "linux-only selftest: all passed"; else echo "linux-only selftest: FAILED"; fi
exit "$fail"
