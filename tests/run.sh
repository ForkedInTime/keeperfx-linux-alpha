#!/bin/sh
# Builds and runs the standalone unit tests. No engine/build dependency: g++
# and the standard library only.
#
#   tests/run.sh
#
# SANITIZE=1 builds the tests with AddressSanitizer + UBSan, so the same
# assertions also police memory errors and undefined behaviour. CI runs both
# modes; the flags change nothing about what is asserted.
#
# Exits non-zero if any build or any test fails.
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT_DIR/bin"

EXTRA_FLAGS=""
if [ "${SANITIZE:-0}" = "1" ]; then
    EXTRA_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
fi

# Pure track-mapping logic, straight out of the header.
g++ -std=c++17 -Wall -Wextra $EXTRA_FLAGS -o "$ROOT_DIR/bin/test_music_index" "$ROOT_DIR/tests/test_music_index.cpp"
"$ROOT_DIR/bin/test_music_index"

# The real Linux directory enumerator, run against a real temporary directory.
# It lives in the platform layer since upstream #5104/#5107 (it was linux.cpp
# before); the enumerator needs SDL3 headers to compile and the tests stub the
# one window-system accessor PlatformManager references but the tests never call.
g++ -std=c++17 -Wall -Wextra $EXTRA_FLAGS -I"$ROOT_DIR/src" $(pkg-config --cflags sdl3) \
	-o "$ROOT_DIR/bin/test_file_find" \
	"$ROOT_DIR/tests/test_file_find.cpp" \
	"$ROOT_DIR/src/kfx/platform/PlatformManager.cpp" \
	"$ROOT_DIR/src/kfx/platform/PlatformLinux.cpp" \
	$(pkg-config --libs sdl3)
"$ROOT_DIR/bin/test_file_find"
