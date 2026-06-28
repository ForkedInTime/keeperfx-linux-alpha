# KeeperFX — Linux Alpha

![KeeperFX Linux Alpha](docs/assets/readme-banner.png)

[![Upstream](https://img.shields.io/badge/upstream-dkfans%2Fkeeperfx-blue?style=flat-square)](https://github.com/dkfans/keeperfx)
![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-1793D1?style=flat-square)
![Render](https://img.shields.io/badge/display-GPU%20OpenGL%203.3-brightgreen?style=flat-square)
![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)

A **native-Linux alpha** of [KeeperFX](https://github.com/dkfans/keeperfx) — the open-source remake of
Bullfrog's *Dungeon Keeper*. It tracks the KeeperFX team's `master` and layers on a handful of personal
improvements: a GPU (OpenGL) display path, truecolor front-end movies, a Wine-free native launcher, a few
modern-Linux / ultrawide fixes, and an in-progress truecolor 3D renderer.

> ⚠️ **Unofficial personal alpha — not affiliated with, or endorsed by, the KeeperFX team.**
> For the official, supported game (Windows, and the canonical cross-platform source), go to
> **[dkfans/keeperfx](https://github.com/dkfans/keeperfx)**. Anything broken here is almost certainly
> *my* change, not theirs — **please don't report issues from this fork upstream.**

---

## Credit — this is the KeeperFX team's game

**KeeperFX is the work of the KeeperFX team and the Keeper Klan community**, built up over many years: the
complete decompilation-and-rewrite of *Dungeon Keeper* into modern code, the gameplay, the tooling — **and the
native Linux support that this alpha stands on.** The overwhelming majority of everything you run here is
theirs. This repository is a thin layer of personal tweaks on top of their `master`, re-synced with their
latest every few months. All the real progress comes from them.

- **Upstream / source of truth:** https://github.com/dkfans/keeperfx
- **Website:** https://keeperfx.net
- **Community (Keeper Klan Discord):** https://discord.gg/hE4p7vy2Hb

If you enjoy this, support **the upstream project** — that's where the game is actually made.

---

## What this alpha adds on top of upstream

Everything below is the only delta from the team's `master`; the rest is 100% theirs.

**Display & media**
- **GPU-accelerated display (OpenGL 3.3).** The game's 8-bit paletted frame is uploaded to the GPU and
  palette-mapped in a shader instead of being blitted on the CPU, then hardware-scaled to your screen
  (great on a 3440×1440 ultrawide). Upstream is still 100% software, even on Linux. This layer is also the
  foundation for the truecolor 3D renderer below.
- **Truecolor front-end movies.** The intro, logo stings and outro can play in full color through the GPU
  (any codec, not just 8-bit Smacker), plus a no-AI "vintage cleanup" of all five movies that keeps the
  original 80s-CG character while removing blocking/banding on a big screen.

**Native Linux launcher** *(the KeeperFX team's `keeperfx-launcher-qt`, made to run natively)*
- The team's Qt settings launcher compiles cross-platform but was written Windows-first (it launched the game
  through **Wine** and read the version from a Windows `.exe`). Here it's built for Linux and patched to detect
  and launch the **native** engine directly — **no Wine** — read the real version, and expose all of its
  Graphics / Sound / Input settings.

**Stability fixes** *(genuine upstream bugs, fixed locally for modern Linux & ultrawide)*
- **Clean exit on quit** — avoids a shutdown segfault caused by the SDL3/Wayland teardown race on systems
  using `sdl2-compat`.
- **Ultrawide creature-possession crash** — the new C++ lens effect didn't size its buffer for large
  horizontal viewports; possessing a creature at 3440×1440 read past the buffer.
- **UTF-8 font heap-crash** — the new UTF-8 font loader freed a pointer into a *static* buffer, aborting on
  glibc the moment the generated `.fxfont` files exist.

**Tooling**
- **`refresh-alpha.sh`** — re-bases these changes onto the team's latest `master`, builds with the correct
  version number, generates the UTF-8 fonts, and deploys. Keeps the alpha current with upstream.

## 🚧 Work in progress

- **Truecolor GPU / Vulkan world renderer + hi-res asset pipeline.** Design docs, a frozen prototype
  world-renderer, and hi-res terrain/sprite plans live under [`docs/vulkan-foundation/`](docs/vulkan-foundation).
  The GPU display layer above is step one; the goal is a real truecolor, eventually 3D-capable renderer —
  developed slowly, on top of whatever the team ships, and always behind a separate switch so the classic
  look stays intact. **None of this is enabled in the current build.**

---

## You still need the original Dungeon Keeper files

Like upstream, this is a standalone engine but **requires the original game files as proof of ownership.** It
will copy them from your own legally-owned install — an old CD, or a digital edition from
[GOG](https://www.gog.com/game/dungeon_keeper),
[EA](https://www.ea.com/games/dungeon-keeper/dungeon-keeper), or
[Steam](https://store.steampowered.com/app/1996630/Dungeon_Keeper_Gold/).

**This project never ships the original Dungeon Keeper data** — only our engine and the free KeeperFX assets.

---

## System requirements

- **A current 64-bit Linux distro: Ubuntu 24.04 / 26.04 or newer, Fedora, Arch, Steam Deck**, etc.
- ⚠️ **Will NOT run on Ubuntu 22.04 or older.** Official builds are compiled on Ubuntu 24.04, whose C
  library (glibc 2.39) is newer than what 22.04 and earlier provide — the binaries simply won't start on
  them. Older systems must [build from source](#advanced-build-from-source) instead.
- An OpenGL 3.3-capable GPU.

## Get started

**The intended experience is one file.** You download a single self-contained **AppImage**, run it, and the
only thing it ever asks for is your own *Dungeon Keeper* files. Everything else — the engine, all its
libraries, the game data, and the settings launcher — is bundled inside. No `apt install`, no dependencies,
nothing to set up.

1. Download **[`KeeperFX-Linux-Alpha-x86_64.AppImage`](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest/download/KeeperFX-Linux-Alpha-x86_64.AppImage)**
   from the latest release (~500 MB — it contains everything).
2. Make it executable and run it:
   ```bash
   chmod +x KeeperFX-Linux-Alpha-x86_64.AppImage
   ./KeeperFX-Linux-Alpha-x86_64.AppImage
   ```

The launcher then auto-detects your Dungeon Keeper install (GOG / Steam / Wine), copies the required files,
lets you tweak graphics/sound/controls, and plays. If it can't find your Dungeon Keeper files, it asks you to
point at them — that's the only thing that can stop it.

> **Won't start with a `libfuse.so.2` error?** Modern distros ship FUSE 3, but AppImages want FUSE 2. Either run
> it without FUSE — `./KeeperFX-Linux-Alpha-x86_64.AppImage --appimage-extract-and-run` — or install the shim
> once: `sudo apt install libfuse2t64` (Ubuntu/Debian) / the `fuse2` package on other distros. This is the
> AppImage *runtime*, not our app — everything our game needs is already bundled.

### Play now — the complete package

1. Download **`keeperfx-linux-alpha-x86_64-full.7z`** from the [latest release](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest).
   It contains the engine + all the free KeeperFX assets + our enhancements (it does **not** contain the
   original Dungeon Keeper files — you supply those).
2. Extract it, e.g. to `~/.local/share/keeperfx-alpha`:
   ```bash
   mkdir -p ~/.local/share/keeperfx-alpha
   7z x keeperfx-linux-alpha-x86_64-full.7z -o/tmp/kfx
   cp -r /tmp/kfx/keeperfx-linux-alpha/. ~/.local/share/keeperfx-alpha/
   ```
3. Add your **own** original *Dungeon Keeper* files (proof of ownership) — see [below](#you-still-need-the-original-dungeon-keeper-files).
   The launcher does this automatically; manually, copy these from your GOG/Steam/CD install into the matching
   folders: `data/` (`bluepal.dat`, `slab0-0.dat`, the palettes…) and `sound/` (`atmos1.sbk`, `atmos2.sbk`, `bullfrog.sbk`).
4. Install the runtime libraries (Arch shown; names vary by distro):
   ```bash
   sudo pacman -S --needed sdl2-compat sdl2_mixer sdl2_net sdl2_image \
     ffmpeg openal luajit libspng minizip zlib libepoxy miniupnpc libnatpmp openssl zstd
   ```
5. Run it:
   ```bash
   cd ~/.local/share/keeperfx-alpha && ./keeperfx
   ```

**The only thing that should stop you** is not having your original Dungeon Keeper files. Everything else is in
the package.

---

## Advanced: build from source

> This section is for developers who want to compile the engine. Most people should use the package above.

Tested on Arch Linux (x86-64). Package **names** differ across distros, but the set is the same.

### 1. Install build dependencies

**Arch / derivatives:**
```bash
sudo pacman -S --needed base-devel git python \
  sdl2-compat sdl2_mixer sdl2_net sdl2_image \
  ffmpeg openal luajit libspng minizip zlib libepoxy \
  miniupnpc libnatpmp openssl zstd
```
> Other distros: you need a C/C++ toolchain, `make`, `git`, `python3`, and the dev packages for
> **SDL2 (+mixer, +net, +image), ffmpeg (avformat/avcodec/avutil/swscale/swresample), OpenAL, LuaJIT, libspng,
> minizip, zlib, libepoxy, miniupnpc, libnatpmp, openssl, zstd.** A few small libraries
> (centijson, astronomy, enet6, libcurl) are fetched and built automatically by the makefile.

### 2. Clone and build the engine

```bash
git clone https://github.com/ForkedInTime/keeperfx-linux-alpha.git
cd keeperfx-linux-alpha

# Fetch the auto-downloaded deps serially first (avoids a parallel-build race)
make -f linux.mk \
  deps/centijson/include/json.h deps/astronomy/include/astronomy.h \
  deps/enet6/include/enet6/enet.h deps/libcurl/lib/libcurl.a

# Build. BUILD_NUMBER = git commit count, exactly as the team's CI computes it,
# so the engine reports a real "1.3.2.<build> alpha" version.
rm -f src/ver_defs.h
make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"
```
This produces **`bin/keeperfx`** (a native ELF).

### 3. Generate the UTF-8 fonts

Upstream's UTF-8 text needs binary font files that the makefile doesn't build. Generate them:
```bash
( cd tools/fxfontmaker && PY=$(command -v python3 || command -v python)
  "$PY" rescale_unifont_hex.py unifont-17.0.04.hex unifont12.hex
  "$PY" bdf_to_hex.py wenquanyi_9pt.bdf wenquanyi.hex
  "$PY" merge_hex.py unifont12.hex wenquanyi.hex merged12.hex
  "$PY" unifont_hex_to_binary.py unifont-17.0.04.hex    font16.fxfont     16
  "$PY" unifont_hex_to_binary.py unifont_jp-17.0.04.hex font16_JPN.fxfont 16
  "$PY" unifont_hex_to_binary.py unifont_t-17.0.04.hex  font16_CHT.fxfont 16
  "$PY" unifont_hex_to_binary.py merged12.hex           font12.fxfont     12
  rm -f merged12.hex wenquanyi.hex unifont12.hex )
```

### 4. Assemble a playable install

This repo contains the **engine source and text config only** — not the playable game data. You need a
KeeperFX **data tree** (free assets: sprites, sounds, campaigns, movies) plus your own Dungeon Keeper files.
The simplest way to get that tree is the **KeeperFX launcher** (or an official KeeperFX install); then overlay
our build on top:

```bash
PREFIX="$HOME/.local/share/keeperfx-alpha"   # your KeeperFX data tree

cp -f bin/keeperfx "$PREFIX/keeperfx"                       # our engine
cp -f tools/fxfontmaker/*.fxfont "$PREFIX/fxdata/"          # UTF-8 fonts
cp -rf config/fxdata/. "$PREFIX/fxdata/"                    # config matching this engine
cp -rf config/creatrs/. "$PREFIX/creatrs/"
cp -rf config/mods/.    "$PREFIX/mods/"
# record the version so the native launcher can read it
sed -n 's/.*VER_STRING  "\(.*\)".*/\1/p' src/ver_defs.h > "$PREFIX/version.txt"

cd "$PREFIX" && ./keeperfx          # play
```

> Steps 2–4 are automated by **`./refresh-alpha.sh`** once you have a data tree at
> `~/.local/share/keeperfx-alpha` and `upstream` set to `https://github.com/dkfans/keeperfx`.

### (Optional) Build the native launcher

The settings launcher is the team's separate repo, [`dkfans/keeperfx-launcher-qt`](https://github.com/dkfans/keeperfx-launcher-qt)
(Qt6 / CMake). It builds on Linux but needs small patches to drive the *native* engine instead of Wine
(native-binary detection, native launch, version-from-`version.txt`, and dropping the Windows-only `-static`
link flag). Those patches are tracked with this project; build with `qt6-base`, `cmake` and `ninja`.

---

## How this alpha stays current with upstream

```bash
git fetch upstream                          # the KeeperFX team's master
git log --oneline HEAD~N..upstream/master   # see what they changed
./refresh-alpha.sh                          # rebase our changes, rebuild, redeploy
```
The few local commits (GPU layer, truecolor movies, the fixes, tooling) re-base cleanly onto their latest
most of the time; occasionally a file we touch needs a quick manual merge.

## License

**GPL-2.0-or-later**, same as upstream KeeperFX — see [LICENSE](LICENSE). All upstream copyrights remain with
the KeeperFX team and contributors. The banner is adapted from the official KeeperFX banner (re-labelled for
this Linux alpha) with thanks. *Dungeon Keeper* is a trademark of its respective owners; this project is not
affiliated with them.
