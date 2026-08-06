#!/usr/bin/env bash
# Build the SDL3 stack from source into a local prefix.
#
# Why this exists: upstream migrated to SDL3, but the release workflows build on
# ubuntu-24.04 and that release has no SDL3 packages. The runner is pinned there
# deliberately -- it gives ffmpeg 6.x and glibc 2.39, and glibc 2.39 is what lets
# the AppImage run on 24.04 and everything newer. Moving the runner forward to
# get SDL3 from apt would raise the glibc floor under every user to buy a
# build-time convenience. Building SDL3 here costs a few cached minutes instead.
#
# The AppImage bundles whatever the engine links against, so the .so files
# installed here are the ones that ship. Distros that already package SDL3
# (Arch, Fedora 41+) need none of this: linux.mk locates SDL3 through pkg-config
# either way, and a system copy is found first unless this prefix is exported.
#
# Usage:  packaging/ci/build-sdl3.sh [prefix]
#         eval "$(packaging/ci/build-sdl3.sh --env [prefix])"   # print exports only
#         packaging/ci/build-sdl3.sh --apt-packages             # what to apt-get first
set -euo pipefail

# Kept here rather than in each workflow so three copies cannot drift apart: what
# SDL3 needs to build is a property of this script, not of any one pipeline.
if [ "${1:-}" = "--apt-packages" ]; then
  echo "cmake \
libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
libwayland-dev wayland-protocols libxkbcommon-dev libdrm-dev libgbm-dev \
libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
libpng-dev libjpeg-dev zlib1g-dev \
libogg-dev libvorbis-dev libflac-dev libmpg123-dev"
  exit 0
fi

# Pinned rather than floating: a silent SDL bump is exactly the kind of change
# that turns a green build into a broken release. Bump deliberately, and update
# the checksum in the same commit.
SDL_VER=3.4.14
SDL_IMAGE_VER=3.4.4
SDL_MIXER_VER=3.2.4

SDL_SHA=30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb
SDL_IMAGE_SHA=29751304a13d25ac513f24305fa25b06a6edd9607718c90129b8350d35fc5573
SDL_MIXER_SHA=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901

# A stable default path so CI can cache it by key.
PREFIX_DEFAULT="$PWD/deps/sdl3"

# CPATH and LIBRARY_PATH are not belt-and-braces, they are required. linux.mk asks
# pkg-config only for --libs-only-l, so it gets "-lSDL3" and never the "-L" that
# would say where that lives, and it passes no SDL cflags at all -- the sources
# just "#include <SDL3/SDL.h>" and trust the default search path. That is fine on a
# distro that packages SDL3 and wrong everywhere else. gcc honours both variables
# natively, so exporting them makes an out-of-tree prefix work without teaching
# linux.mk about a layout that only CI ever sees.
if [ "${1:-}" = "--env" ]; then
  PREFIX="$(cd "${2:-$PREFIX_DEFAULT}" 2>/dev/null && pwd || echo "${2:-$PREFIX_DEFAULT}")"
  # ${VAR:+:$VAR} appends the previous value only when there IS one. The obvious
  # "$new:${VAR:-}" instead leaves a trailing colon whenever the variable was
  # unset -- which is the normal CI case -- and an empty element in any of these
  # search paths means the current directory. That would silently put the repo
  # root ahead of /usr/include for every "#include <...>" in the build.
  emit() { echo "export $1=\"$2\${$1:+:\$$1}\""; }
  emit PKG_CONFIG_PATH "$PREFIX/lib/pkgconfig"
  emit CPATH           "$PREFIX/include"
  emit LIBRARY_PATH    "$PREFIX/lib"
  emit LD_LIBRARY_PATH "$PREFIX/lib"
  exit 0
fi

PREFIX="${1:-$PREFIX_DEFAULT}"
BUILD="$PREFIX/.build"
STAMP="$PREFIX/.stamp-$SDL_VER-$SDL_IMAGE_VER-$SDL_MIXER_VER"

# The stamp encodes all three versions, so a version bump invalidates a restored
# cache instead of silently reusing the old libraries.
if [ -f "$STAMP" ]; then
  echo "SDL3 $SDL_VER / image $SDL_IMAGE_VER / mixer $SDL_MIXER_VER already built in $PREFIX"
  exit 0
fi

mkdir -p "$BUILD"

fetch() {   # fetch <url> <file> <sha256>
  local url="$1" out="$2" want="$3"
  if [ ! -f "$out" ]; then
    echo "==> fetching $(basename "$out")"
    curl -fsSL --retry 3 -o "$out.part" "$url"
    mv "$out.part" "$out"
  fi
  local got
  got="$(sha256sum "$out" | cut -d' ' -f1)"
  if [ "$got" != "$want" ]; then
    echo "!! checksum mismatch for $(basename "$out")" >&2
    echo "   expected $want" >&2
    echo "   got      $got" >&2
    exit 1
  fi
}

build() {   # build <srcdir> <extra cmake args...>
  local src="$1"; shift
  cmake -S "$src" -B "$src/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON \
    "$@" >/dev/null
  cmake --build "$src/build" --parallel "$(nproc)" >/dev/null
  cmake --install "$src/build" >/dev/null
}

base=https://github.com/libsdl-org

fetch "$base/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz" \
      "$BUILD/SDL3-$SDL_VER.tar.gz" "$SDL_SHA"
fetch "$base/SDL_image/releases/download/release-$SDL_IMAGE_VER/SDL3_image-$SDL_IMAGE_VER.tar.gz" \
      "$BUILD/SDL3_image-$SDL_IMAGE_VER.tar.gz" "$SDL_IMAGE_SHA"
fetch "$base/SDL_mixer/releases/download/release-$SDL_MIXER_VER/SDL3_mixer-$SDL_MIXER_VER.tar.gz" \
      "$BUILD/SDL3_mixer-$SDL_MIXER_VER.tar.gz" "$SDL_MIXER_SHA"

for t in "$BUILD"/*.tar.gz; do tar -xzf "$t" -C "$BUILD"; done

echo "==> building SDL3 $SDL_VER"
build "$BUILD/SDL3-$SDL_VER" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF

# VENDORED=OFF: the release tarballs ship an EMPTY external/ plus a download
# script, so vendoring would mean fetching unpinned submodules over the network
# mid-build. System -dev packages are pinned by the runner image instead; the
# workflows install them alongside the other build deps.
#
# DEPS_SHARED=OFF is the load-bearing flag, and it does not mean static linking:
# it means "link the decoders normally" instead of dlopen'ing them by name at
# runtime. That matters because the AppImage is assembled by linuxdeploy, which
# discovers libraries by walking ldd output -- a dlopen'd libvorbisfile is
# invisible to it and would simply be missing from the bundle, so music would
# die on a user's machine while working perfectly in CI. Linking them normally
# puts them in DT_NEEDED, where linuxdeploy finds and ships them.
echo "==> building SDL3_image $SDL_IMAGE_VER"
build "$BUILD/SDL3_image-$SDL_IMAGE_VER" \
  -DSDLIMAGE_VENDORED=OFF -DSDLIMAGE_DEPS_SHARED=OFF \
  -DSDLIMAGE_SAMPLES=OFF -DSDLIMAGE_TESTS=OFF \
  -DSDLIMAGE_PNG=ON -DSDLIMAGE_JPG=ON \
  -DSDLIMAGE_AVIF=OFF -DSDLIMAGE_TIF=OFF -DSDLIMAGE_WEBP=OFF

# The game needs OGG for its own music and FLAC/WAV/MP3 for user-supplied tracks.
echo "==> building SDL3_mixer $SDL_MIXER_VER"
build "$BUILD/SDL3_mixer-$SDL_MIXER_VER" \
  -DSDLMIXER_VENDORED=OFF -DSDLMIXER_DEPS_SHARED=OFF \
  -DSDLMIXER_TESTS=OFF -DSDLMIXER_EXAMPLES=OFF \
  -DSDLMIXER_VORBIS_VORBISFILE=ON -DSDLMIXER_FLAC=ON -DSDLMIXER_MP3=ON -DSDLMIXER_WAVE=ON \
  -DSDLMIXER_OPUS=OFF -DSDLMIXER_WAVPACK=OFF -DSDLMIXER_GME=OFF -DSDLMIXER_MOD=OFF

# Prove the three .pc files the engine's linux.mk asks for are actually there
# before declaring success -- a partial install that still stamps itself done
# would fail much later, in a link step, with a far less obvious message.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
for m in sdl3 sdl3-image sdl3-mixer; do
  v="$(pkg-config --modversion "$m")" || { echo "!! $m.pc missing from $PREFIX" >&2; exit 1; }
  echo "    $m $v"
done

touch "$STAMP"
# Sources and tarballs together are ~500 MB unpacked and nothing downstream reads
# them; keeping them would bloat the very CI cache this prefix exists to fill.
rm -rf "$BUILD"
echo "==> SDL3 stack installed into $PREFIX"
