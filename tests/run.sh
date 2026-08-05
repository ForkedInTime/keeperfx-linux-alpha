#!/bin/sh
# Builds and runs the standalone unit tests. No engine/build dependency: g++
# and the standard library only.
#
#   tests/run.sh
#
# Exits non-zero if any build or any test fails.
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT_DIR/bin"

# Pure track-mapping logic, straight out of the header.
g++ -std=c++17 -Wall -Wextra -o "$ROOT_DIR/bin/test_music_index" "$ROOT_DIR/tests/test_music_index.cpp"
"$ROOT_DIR/bin/test_music_index"

# The real Linux directory enumerator, run against a real temporary directory.
# linux.cpp needs two accommodations to link without the engine: its main() is
# renamed out of the way so the test can define its own, and the two engine
# functions it references are stubbed in the test itself.
g++ -std=c++17 -Wall -Wextra -I"$ROOT_DIR/src" -Dmain=keeperfx_linux_main_unused \
	-c "$ROOT_DIR/src/linux.cpp" -o "$ROOT_DIR/bin/linux_for_tests.o"
g++ -std=c++17 -Wall -Wextra -I"$ROOT_DIR/src" -o "$ROOT_DIR/bin/test_file_find" \
	"$ROOT_DIR/tests/test_file_find.cpp" "$ROOT_DIR/bin/linux_for_tests.o"
"$ROOT_DIR/bin/test_file_find"
