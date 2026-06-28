# KeeperFX — Linux Alpha

![KeeperFX Linux Alpha](docs/assets/readme-banner.png)

[![Upstream](https://img.shields.io/badge/upstream-dkfans%2Fkeeperfx-blue?style=flat-square)](https://github.com/dkfans/keeperfx)
![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-1793D1?style=flat-square)
![Render](https://img.shields.io/badge/display-GPU%20OpenGL%203.3-brightgreen?style=flat-square)
![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)

**Dungeon Keeper, native on Linux. One file — download, run, play.**

## ▶ Download &amp; play

### **[⬇ KeeperFX-Linux-Alpha-x86_64.AppImage](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest/download/KeeperFX-Linux-Alpha-x86_64.AppImage)** &nbsp;·&nbsp; ~500 MB &nbsp;·&nbsp; any current 64-bit Linux

<sub>↳ The link above always grabs the newest build. To see every version, the changelogs, or an older build, browse **[all releases »](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases)**.</sub>

```bash
chmod +x KeeperFX-Linux-Alpha-x86_64.AppImage
./KeeperFX-Linux-Alpha-x86_64.AppImage
```

That **one file** has everything — launcher, engine, every library, and the game data. Nothing to `apt install`.
It opens the settings launcher, finds your *Dungeon Keeper* install, lets you set up graphics/sound, and plays.

🔑 **The only thing you provide is your own original *Dungeon Keeper* files** — from
[GOG](https://www.gog.com/game/dungeon_keeper),
[Steam](https://store.steampowered.com/app/1996630/Dungeon_Keeper_Gold/),
[EA](https://www.ea.com/games/dungeon-keeper/dungeon-keeper), or an old CD. The launcher copies them for you.
(We never ship the original game — you must own it.)

> 🐧 **No GOG Galaxy on Linux?** There's no native GOG client — so see
> **[How do I install DK1 on Linux?](#how-do-i-install-dk1-on-linux)** for getting your GOG/Steam/EA
> copy installed (via Lutris or Heroic) so the launcher can find it.

Runs on any current 64-bit distro — Ubuntu 24.04 / 26.x, Fedora, Arch, Steam Deck, … &nbsp;
*(not Ubuntu 22.04 or older — see [requirements](#system-requirements)).*

<details>
<summary><b>Won't start?</b> (a <code>libfuse.so.2</code> error)</summary>

> Modern distros ship FUSE 3, but AppImages want FUSE 2 to mount themselves. Either:
> - run it without FUSE: `./KeeperFX-Linux-Alpha-x86_64.AppImage --appimage-extract-and-run`, or
> - install the small shim once: `sudo apt install libfuse2t64` (Ubuntu/Debian) / the `fuse2` package elsewhere.
>
> That's the AppImage *runtime*, not our app — everything our game needs is already bundled.
</details>

<details>
<summary><b>Prefer Flatpak?</b> &nbsp;(AppImage vs Flatpak — which should I pick?)</summary>

> Both contain the same game; they're just two ways to install it.
>
> - **AppImage — recommended.** The simplest: download one file, make it runnable, go. Nothing is installed
>   into your system, and you can delete it any time. Best if you just want to play.
> - **Flatpak.** Pick this if you already use Flatpak and want KeeperFX in your app menu with automatic
>   updates. It needs Flatpak set up first and downloads a shared system runtime the first time (a few hundred
>   MB, reused by your other Flatpaks). It's newer and less-tested for this game, so if anything misbehaves,
>   use the AppImage. Grab `KeeperFX-Linux-Alpha-x86_64.flatpak` from
>   [Releases](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases) and:
>   `flatpak install --user ./KeeperFX-Linux-Alpha-x86_64.flatpak`
>
> **Short version: use the AppImage unless you specifically want Flatpak.**
</details>

> ⚠️ *Unofficial personal alpha — **not** affiliated with the KeeperFX team. Please don't report issues from
> this fork upstream. The game itself is [their work](#credit). Prefer to compile it yourself, or want the
> details? Read on.*

---

<sub>Everything below is detail — you don't need it to play.</sub>

## Credit

**KeeperFX is the work of the KeeperFX team and the Keeper Klan community**, built up over many years: the
complete decompilation-and-rewrite of *Dungeon Keeper* into modern code, the gameplay, the tooling — **and the
native Linux support that this alpha stands on.** The overwhelming majority of everything you run here is
theirs. This repository is a thin layer of personal tweaks on top of their `master`, re-synced with their
latest every few months. All the real progress comes from them.

- **Upstream / source of truth:** https://github.com/dkfans/keeperfx
- **Website:** https://keeperfx.net
- **Community (Keeper Klan Discord):** https://discord.gg/hE4p7vy2Hb

If you enjoy this, support **the upstream project** — that's where the game is actually made.

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
- **Built-in updates.** The launcher checks *this* repo's releases and shows **"Update available — vX"** when a
  newer build is out; one click downloads just the updated game package (no need to re-download the whole
  AppImage). It notifies — it never auto-overwrites.

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

## How do I install DK1 on Linux?

KeeperFX is a standalone engine but **needs the original *Dungeon Keeper* files as proof of ownership** — we
never ship the original game, only our engine and the free KeeperFX assets. The catch on Linux:
[GOG](https://www.gog.com/game/dungeon_keeper),
[EA](https://www.ea.com/games/dungeon-keeper/dungeon-keeper) and
[Steam](https://store.steampowered.com/app/1996630/Dungeon_Keeper_Gold/) only sell *Dungeon Keeper* as a
**Windows** program, and there's no GOG client for Linux. But you only need its **data files**, and getting them
is easy. Pick whichever fits you; once DK is installed, run the KeeperFX AppImage and the launcher finds it
automatically.

**Option A — Lutris (easiest, fully automatic).**
[Lutris](https://lutris.net) has a one-click community installer that downloads and installs *Dungeon Keeper
Gold* from GOG for you (it handles Wine behind the scenes — you don't deal with any of it).
1. Install Lutris from your distro's software store.
2. In Lutris, search **"Dungeon Keeper"**, pick the GOG install script, and log in with your GOG account.
3. Let it install. The KeeperFX launcher then auto-detects it.

**Option B — Heroic Games Launcher.**
[Heroic](https://heroicgameslauncher.com) is a friendly Linux client for GOG and Epic. Log into GOG, install
*Dungeon Keeper Gold*, done.

**Option C — Extract the GOG offline installer directly (no Wine at all).**
If you downloaded the GOG **offline installer** `.exe`, you don't even have to run it — just unpack it:
```bash
sudo apt install innoextract        # Debian/Ubuntu  (or 'innoextract' on your distro)
innoextract setup_dungeon_keeper_gold_*.exe
```
This dumps the game into an `app/` folder. Point the launcher at it (or copy its `data/` and `sound/` files into
the install). This is the lightest option — no GOG client, no Wine.

**Option D — Steam + Proton.**
Buy *Dungeon Keeper Gold* on [Steam](https://store.steampowered.com/app/1996630/Dungeon_Keeper_Gold/), enable
**Proton** for it (Properties → Compatibility → *Force the use of a specific Steam Play compatibility tool*),
and install. The launcher detects the Steam copy.

> The launcher already looks in the usual places — Lutris, Steam, and `~/.wine` GOG/EA prefixes. If it can't
> find your install, it simply asks you to browse to the folder. **This is the one and only thing you have to
> provide — everything else is in the download.**

## System requirements

- **A current 64-bit Linux distro: Ubuntu 24.04 / 26.04 or newer, Fedora, Arch, Steam Deck**, etc.
- ⚠️ **Will NOT run on Ubuntu 22.04 or older.** Official builds are compiled on Ubuntu 24.04, whose C
  library (glibc 2.39) is newer than what 22.04 and earlier provide — the binaries simply won't start on
  them. Older systems must [build from source](#build-from-source) instead.
- An OpenGL 3.3-capable GPU.

## Other ways to install

<details>
<summary><b>The complete package</b> (engine + data, you add libraries + DK files)</summary>

1. Download **`keeperfx-linux-alpha-x86_64-full.7z`** from the [latest release](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest).
2. Extract it to `~/.local/share/keeperfx-alpha`:
   ```bash
   mkdir -p ~/.local/share/keeperfx-alpha
   7z x keeperfx-linux-alpha-x86_64-full.7z -o/tmp/kfx
   cp -r /tmp/kfx/keeperfx-linux-alpha/. ~/.local/share/keeperfx-alpha/
   ```
3. Add your **own** Dungeon Keeper files into `data/` (`bluepal.dat`, `slab0-0.dat`, the palettes…) and
   `sound/` (`atmos1.sbk`, `atmos2.sbk`, `bullfrog.sbk`).
4. Install the runtime libraries (Arch shown; names vary by distro):
   ```bash
   sudo pacman -S --needed sdl2-compat sdl2_mixer sdl2_net sdl2_image \
     ffmpeg openal luajit libspng minizip zlib libepoxy miniupnpc libnatpmp openssl zstd
   ```
5. Run: `cd ~/.local/share/keeperfx-alpha && ./keeperfx`
</details>

### Build from source

<details>
<summary>For developers who want to compile the engine themselves</summary>

Tested on Arch Linux (x86-64). Package **names** differ across distros, but the set is the same.

**1. Install build dependencies** (Arch / derivatives):
```bash
sudo pacman -S --needed base-devel git python \
  sdl2-compat sdl2_mixer sdl2_net sdl2_image \
  ffmpeg openal luajit libspng minizip zlib libepoxy \
  miniupnpc libnatpmp openssl zstd
```
> Other distros: a C/C++ toolchain, `make`, `git`, `python3`, and the dev packages for SDL2 (+mixer/net/image),
> ffmpeg (avformat/avcodec/avutil/swscale/swresample), OpenAL, LuaJIT, libspng, minizip, zlib, libepoxy,
> miniupnpc, libnatpmp, openssl, zstd. centijson/astronomy/enet6/libcurl are fetched by the makefile.

**2. Clone and build the engine:**
```bash
git clone https://github.com/ForkedInTime/keeperfx-linux-alpha.git
cd keeperfx-linux-alpha
make -f linux.mk \
  deps/centijson/include/json.h deps/astronomy/include/astronomy.h \
  deps/enet6/include/enet6/enet.h deps/libcurl/lib/libcurl.a
rm -f src/ver_defs.h
make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"
```
This produces **`bin/keeperfx`** (a native ELF).

**3. Generate the UTF-8 fonts** (the makefile doesn't build them):
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

**4. Assemble a playable install.** This repo has engine source + text config only — not the game data. Get a
KeeperFX data tree (via the launcher or an official install), then overlay your build + fonts + `config/` and
write `version.txt`. The whole rebase-build-deploy is automated by **`./refresh-alpha.sh`**.

The native launcher is its own repo:
[**keeperfx-launcher-qt-linux**](https://github.com/ForkedInTime/keeperfx-launcher-qt-linux).
</details>

## How this alpha stays current with upstream

```bash
git fetch upstream                          # the KeeperFX team's master
git log --oneline HEAD~N..upstream/master   # see what they changed
./refresh-alpha.sh                          # rebase our changes, rebuild, redeploy
```
The few local commits re-base cleanly onto their latest most of the time; occasionally a file we touch needs a
quick manual merge. The CI then rebuilds the AppImage **and Flatpak** automatically on each new release.

## ❓ FAQ

### How do I install DK1 on Linux?

KeeperFX needs your **own original *Dungeon Keeper* files** as proof of ownership — the launcher copies
them out of a real install. On Windows you'd just run GOG Galaxy; **there is no GOG Galaxy for Linux**, so
you install your GOG/Steam/EA copy with a Wine front-end first, then point the launcher at it.

**Option A — Lutris (recommended for GOG)**
1. Install Lutris: `sudo apt install lutris` (Ubuntu/Debian), `sudo pacman -S lutris` (Arch), or the Flatpak.
2. Link your GOG account: **Preferences → Sources → GOG → Sign in**. This lets Lutris pull the installer
   automatically.
3. Search **Dungeon Keeper** on [lutris.net](https://lutris.net/games/dungeon-keeper/), click **Install**,
   and follow the script.
4. **If Lutris can't auto-download** (account not linked, offline installer, GOG API hiccup), it shows a
   file picker. Point it at the GOG **setup `.exe`** you downloaded from gog.com (plus any `.bin` parts) —
   *not* an existing install folder. Lutris installs the game fresh into its own Wine prefix.

**Option B — Heroic Games Launcher**
1. Install Heroic (`flatpak install flathub com.heroicgameslauncher.hgl`, or the AppImage).
2. Log in to **GOG** in Heroic's sidebar.
3. Select *Dungeon Keeper* from your GOG library and **Install** — Heroic downloads it and sets up the
   Wine prefix for you.

**Then connect it to KeeperFX**

The KeeperFX launcher tries to **auto-detect** your install and copy the game files. If it can't find them,
it asks you to point it at the folder yourself — so locate the install inside the Wine prefix, typically:

- **Lutris:** `~/Games/dungeon-keeper/drive_c/GOG Games/Dungeon Keeper/` (or wherever you installed it)
- **Heroic:** `~/Games/Heroic/Dungeon Keeper/drive_c/...`

That folder (and its `data/` and `sound/` subfolders) is what the launcher copies from. You only do this
once; after that KeeperFX runs natively with **no Wine**.

> 💡 You don't keep using Lutris/Heroic to *play* — they're only there to install the original game so
> KeeperFX can borrow its data files. Once copied, launch KeeperFX directly.

## License

**GPL-2.0-or-later**, same as upstream KeeperFX — see [LICENSE](LICENSE). All upstream copyrights remain with
the KeeperFX team and contributors. The banner is adapted from the official KeeperFX banner (re-labelled for
this Linux alpha) with thanks. *Dungeon Keeper* is a trademark of its respective owners; this project is not
affiliated with them.
