#!/bin/sh
# Builds and runs the standalone music_index unit tests. No engine/build
# dependency: g++ and the standard library only.
#
#   tests/run.sh
#
# Exits non-zero if the build or the tests fail.
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT_DIR/bin"

g++ -std=c++17 -Wall -Wextra -o "$ROOT_DIR/bin/test_music_index" "$ROOT_DIR/tests/test_music_index.cpp"
"$ROOT_DIR/bin/test_music_index"
