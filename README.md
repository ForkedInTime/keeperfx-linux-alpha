# KeeperFX — Tux Edition

*The native Linux build of KeeperFX — built for penguins. 🐧 &nbsp;Unofficial, community-maintained, and continuously re-synced with the upstream team's work.*

![KeeperFX Tux Edition](docs/assets/readme-banner.png)

[![Upstream](https://img.shields.io/badge/upstream-dkfans%2Fkeeperfx-blue?style=flat-square)](https://github.com/dkfans/keeperfx)
![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-1793D1?style=flat-square)
![Render](https://img.shields.io/badge/display-GPU%20OpenGL%203.3-brightgreen?style=flat-square)
![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)

**Dungeon Keeper, native on Linux.** One file on most distros — a proper AUR package on Arch.

## ▶ Download &amp; play

### **[⬇ KeeperFX-Linux-Alpha-x86_64.AppImage](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest/download/KeeperFX-Linux-Alpha-x86_64.AppImage)** &nbsp;·&nbsp; ~500 MB &nbsp;·&nbsp; any current 64-bit Linux

<sub>↳ The link above always grabs the newest **stable** build. To see every version, the changelogs, or an older build, browse **[all releases »](https://github.com/ForkedInTime/keeperfx-linux-alpha/releases)**.</sub>

> 🧭 **Two channels: stable and alpha.** The download above is **stable** — it only moves once a build has
> been played and left alone for a while. **Alpha** is where the work happens and can change under you;
> its releases are tagged `-alpha`. The launcher has a release-channel setting in its Settings dialog, and
> it only ever offers you releases from the channel you picked. Not sure? Stay on stable.

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

## 🏛 Arch Linux — install from the AUR

**Arch, CachyOS, EndeavourOS, Manjaro:** there's a proper package —
[**keeperfx-tux** on the AUR](https://aur.archlinux.org/packages/keeperfx-tux). No blob to download, no
`chmod +x` — the engine is compiled from source against your system's own libraries.

```bash
yay -S keeperfx-tux
```

That one command is the whole install — **the launcher and game data come with it**, not as separate
steps: `keeperfx-tux` pulls in `keeperfx-tux-data` (campaigns, graphics, sounds) and
`keeperfx-tux-launcher` (the Qt launcher) automatically, all locked to the same version.

**First launch: open “KeeperFX” from your menu — that's the launcher, and it finishes the setup.**
It finds your *Dungeon Keeper* installation (GOG, Steam, EA, Lutris/Heroic) and copies the handful of
original files the packages legally can't ship, fetches the soundtrack if your copy lacks it, and is
also where the Workshop browser and Mod Manager live — one-click campaigns, maps and mods. The second
menu entry, **“KeeperFX (play directly)”**, skips the launcher and starts the game itself: fine for
every day *after* setup, but on a fresh install it will simply stop and list the files it's missing.

Updates arrive with your normal `yay -Syu` — the AUR tracks this project's **stable** releases, so an
upgrade only ever moves you between stables, never onto an alpha. Prefer to build without an AUR
helper? `git clone` this repo and `makepkg -si` in `packaging/aur/` produces the identical packages —
same recipe.

<details>
<summary><b>What the package does and doesn't include</b></summary>

> It installs as two pieces from one recipe: `keeperfx-tux`, the engine, compiled against your system's
> SDL3/ffmpeg/OpenAL rather than bundling copies; and `keeperfx-tux-data`, everything KeeperFX itself
> provides — campaigns, graphics, sounds and language files. You only ever ask for the first; pacman
> brings the second.
>
> On first launch the game directory is assembled at `~/.local/share/keeperfx-alpha` (set `KEEPERFX_HOME`
> to put it elsewhere). **You still need to own the original game**, same as every other install method
> here: 14 files from your *Dungeon Keeper* CD or GOG/Steam/EA copy are not redistributable, so they are
> not in any package. If they are missing the game names them exactly and stops, and the Qt launcher can
> find an installation and copy them for you.
>
> The recipe lives in `packaging/aur/` in this repository, so what you build with `makepkg` above is
> exactly what will be published to the AUR — same PKGBUILD, same two packages, same paths.

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
>   *(The Flatpak tracks the **stable** channel and is rebuilt monthly, so it may briefly trail a
>   brand-new stable — no matter: it self-updates the game package on launch.)*
>
> **Short version: use the AppImage unless you specifically want Flatpak.**
</details>

> ⚠️ *Unofficial personal fork — **not** affiliated with the KeeperFX team. Please don't report issues from
> this fork upstream. The game itself is [their work](#credit). Prefer to compile it yourself, or want the
> details? Read on.*

---

<sub>Everything below is detail — you don't need it to play.</sub>

## Credit

**KeeperFX is the work of the KeeperFX team and the Keeper Klan community**, built up over many years: the
complete decompilation-and-rewrite of *Dungeon Keeper* into modern code, the gameplay, the tooling — **and the
native Linux support this edition stands on.** The overwhelming majority of everything you run here is
theirs. This repository is a thin layer of personal tweaks on top of their `master`, re-synced with their
latest **weekly** (see [how](#how-the-tux-edition-stays-current-with-upstream)). All the real progress comes from them.

- **Upstream / source of truth:** https://github.com/dkfans/keeperfx
- **Website:** https://keeperfx.net
- **Community (Keeper Klan Discord):** https://discord.gg/hE4p7vy2Hb

If you enjoy this, support **the upstream project** — that's where the game is actually made.

## What the Tux Edition adds on top of upstream

**Upstream supports Linux — as source code.** `linux.mk` is theirs and it works; you can compile KeeperFX
on Linux today. What you can't do is *download* it. Their release pipeline cross-compiles to Windows with
MinGW, and every release from 2024 to the current **v1.4.0** ships exactly one file:
`keeperfx_1_4_0_complete.7z`, a Windows package. Their download page lists Linux as *"Not available yet —
coming soon"*, and there's an open pull request to build an AppImage on their CI
([#4990](https://github.com/dkfans/keeperfx/pull/4990)) — good news, and we'll happily stand down the day
it lands. Until then the Linux path ends at a compiler: no build, no installer, no launcher.

**This edition is that missing half, and only that half.** Every week a bot merges the team's latest
`master` here, compile-checks it and opens a pull request, so the *game* is theirs and stays current —
never a stale snapshot frozen at whatever built cleanly once. On top of it sits the part nobody else
maintains: a ready-to-run **AppImage, Flatpak and Arch package**, a native Qt launcher that doesn't touch
Wine, and the Linux-specific fixes, hardening and performance work below.

> **At a glance — everything here is a curated Linux layer, not a rewrite of the game.** We don't chase
> upstream's headline count; the team ships the *game*, and our job is the platform work they don't do.
> That layer is real, and every line of it is traceable to a commit in this repo:
>
> | | What we added | Roughly |
> |---|---|---|
> | 🐧 | **Ready-to-run Linux builds** — one-file AppImage, Flatpak, and an Arch/AUR package, plus the native Qt launcher (upstream ships source only — no Linux binary in any release) | the whole platform |
> | 🛡️ | **Correctness & security hardening** — a multi-agent Linux audit (out-of-bounds writes from crafted maps/mods, union byte-aliasing, format-string bugs) plus a standing AddressSanitizer pass that found five memory faults upstream has shipped since as far back as 2008 | ~34 fixes |
> | 💥 | **Crash fixes** — ultrawide creature-possession, UTF-8 fonts, campaign scripts, clean exit, case-sensitive audio, the creature-list corruption **root-fixed** | ~7 fixes |
> | ⚡ | **Performance** — frame pacing matched to your monitor, cached parchment map view, GPU palette re-upload, per-turn CPU busy-spin, sprite/text blit, GUI hot paths, cached instant-load Workshop | 9 wins |
> | 🎨 | **Graphics & audio** — GPU OpenGL 3.3 present layer, truecolor movie playback, your own music in any filenames and any of OGG/FLAC/WAV/MP3, a real window icon and desktop identity on X11 *and* Wayland | 4 items |
> | 🌐 | **Multiplayer map packs** — the Classic, Modern and Original mappacks now load in every install method | 1 fix |
> | 🧰 | **Launcher & tooling** — in-launcher Workshop browser + Installed manager, Mod Manager, Play ▾ menu, built-in updater with **separate stable and alpha channels**, side-by-side log viewer, music download + recovery, single-instance lock, weekly sync bot | 12+ items |
>
> <sub>Count it yourself: `git log --oneline --no-merges upstream/master..HEAD` — 185 commits of ours on top
> of theirs, on top of 16 upstream merges. The sections below are the line items.</sub>

<details>
<summary><b>📋 Full breakdown — every change, area by area</b> &nbsp;<sub>(click to expand)</sub></summary>
<br>

**Display & media**
- **GPU-accelerated display (OpenGL 3.3).** The game's 8-bit paletted frame is uploaded to the GPU and
  palette-mapped in a shader instead of being blitted on the CPU, then hardware-scaled to your screen
  (great on a 3440×1440 ultrawide). Upstream is still 100% software, even on Linux. This layer is also the
  foundation for the truecolor 3D renderer below.
- **Truecolor front-end movies.** The intro, logo stings and outro can play in full color through the GPU
  (any codec, not just 8-bit Smacker), plus a no-AI "vintage cleanup" of all five movies that keeps the
  original 80s-CG character while removing blocking/banding on a big screen.
- **Your own music — any filenames, any format.** Upstream demands exactly `keeper02.ogg` … `keeper07.ogg`
  and plays nothing otherwise. Here the game uses whatever audio is in `music/`: name the files what you
  like (`Track 02.flac`, `02.wav`, or the original names) in **OGG, FLAC, WAV or MP3**. Files carrying a
  track number are placed by it; the rest are used in alphabetical order. Existing installs are untouched —
  `keeperNN` names are still looked up first and still win. And when something doesn't play, `keeperfx.log`
  now names every file the game skipped and why, instead of failing silently.
- **A real window icon and desktop identity.** The game window came up with a generic placeholder icon in
  the taskbar and dock. It now sets its own icon *and* a matching desktop entry — Wayland ignores the icon
  a window asks for and instead matches the window's app ID to an installed `.desktop` file, so both halves
  are needed for the icon to actually appear. Fixed for X11 and Wayland alike, in every install method.

**In-game quality of life**
- **Delete a saved game, and get it back.** The engine never could delete one — the only way to
  reclaim a slot was to overwrite it blind. Every slot on the Save and Load menus now carries a
  skull button (Bullfrog's own "dead keeper" symbol) that asks first and **names the save it is
  about to delete**. Deleting moves the save to your **desktop Trash**, where *Restore* puts it
  back where it belongs — it is the freedesktop standard, so your own file manager handles it. In
  a sandbox that cannot reach a real Trash, the game keeps its own instead: the 10 most recent
  deletions for 30 days, whichever comes first, both configurable. The list also closes up when
  you delete, rather than leaving an `UNUSED` hole in the middle of it. Clicking away from a
  save-name you are typing no longer freezes the menu, in any text field in the game.
- **Your saved games survive an engine update.** A save is a raw copy of the engine's internal
  state, so a single field added anywhere inside it makes every earlier save unreadable — the
  bytes are intact, they simply mean something different to the newer engine. Upstream added one
  four-byte field once and every save in existence stopped loading, silently. Three things now
  stand in the way: loading an incompatible save **says so and returns you to the menu** instead
  of the game quitting outright; **the engine that wrote your saves is kept** when you update
  (three releases deep, on all three install methods) so those campaigns stay playable; and the
  build **refuses to release** at all if the save format has moved without someone deciding it
  should. Saves are never modified or deleted by any of this — a save always opens in the version
  that wrote it.

**Native Linux launcher** *(the KeeperFX team's `keeperfx-launcher-qt`, made to run natively)*
- The team's Qt settings launcher compiles cross-platform but was written Windows-first (it launched the game
  through **Wine** and read the version from a Windows `.exe`). Here it's built for Linux and patched to detect
  and launch the **native** engine directly — **no Wine** — read the real version, and expose all of its
  Graphics / Sound / Input settings.
- **Built-in updates.** The launcher checks *this* repo's releases and shows **"Update available — vX"** when a
  newer build is out; one click downloads just the updated game package (no need to re-download the whole
  AppImage). It notifies — it never auto-overwrites.
- **One-click mods, campaigns & map packs.** A Mod Manager with an **Install…** button takes a `.7z`/`.zip`
  (e.g. a [keeperfx.net workshop](https://keeperfx.net/workshop) download) and drops it into the right place —
  mods into `mods/`, campaigns into `campgns/`, map packs into `levels/` — generating a `mod.cfg` if the
  archive lacks one. Mods get an Enabled toggle that writes the load order for you.
- **Browse & install from the Workshop, in the launcher.** A **Browse Workshop** button opens the full
  [keeperfx.net workshop](https://keeperfx.net/workshop) catalogue *inside* the launcher — search, filter by
  category, sort by rating / downloads, thumbnails, and a **Details** link to each item's page — and **install
  with one click** (maps, campaigns, map packs and mods each land in the right place — including standalone
  single maps, and VCS/OS junk like `.git` is stripped from archives). It sorts by rating / downloads /
  newest and marks what you already have. An **Installed** tab scans what you have, labels each add-on as
  KeeperFX-stock or Workshop/user, and lets you **uninstall reversibly** — removed items go to a backup you
  can **Restore** in one click. And it **loads instantly on re-open**: thumbnails and the catalogue are
  cached to disk and refreshed in the background, so there's no "is it stuck?" wait after the first visit.
- **Read the logs without leaving the launcher.** Play ▾ → **View logs** shows the launcher log and
  the game log side by side, following live, with a Copy button — and errors now offer a **Show log**
  button directly. The news panel lists this fork's releases (version and date, one line each, the
  full changelog one click away) above the KeeperFX team's news.
- **One launcher at a time.** The launcher won't open a second copy of itself (that would let two games fight
  over the same saves).
- **Accessibility — launcher size.** Scale the whole launcher UI (100 %, 110 %, 125 % … up to 200 %) for
  readability.

**Linux performance** *(engine-level optimizations specific to this native build — upstream is Windows-first and does not tune the GCC/Linux path, so these are ours)*
- **Frame pacing matched to your monitor.** The shipped default draws as fast as the processor allows —
  frames your display never shows, one core pinned at 100%, and, because the game runs on a single thread,
  nothing held in reserve for a frame that needs extra work. Such a frame overruns and is dropped: an
  intermittent stutter with no single cause. New installs now cap drawing at the refresh rate the display
  reports, and when the machine can't hold that rate the cap steps down to a whole fraction of it
  (144 → 72 → 48) so a frame that would have been dropped arrives on time instead. It has hysteresis in
  both directions, so it settles rather than hunts. A rate you set yourself is an instruction and is never
  overridden, and the simulation is untouched — multiplayer and replays advance at their own pace no matter
  how often the screen is drawn.
- **The map view no longer costs a processor core.** With the map open, 46% of all CPU time went on
  stretching the parchment background to fill the screen — about five million pixels at 3440×1440, every
  frame, on the one thread the game loop runs on — only to discard it and start over. The image is static.
  It's now scaled once and kept, rebuilt only when the screen size, scale, map geometry or the image itself
  actually changes. The cost scaled with screen area, so the wider your display the more this gives back.
- **No redundant GPU palette re-upload.** The OpenGL present path re-uploaded the 256-colour palette every
  frame — a CPU expansion, a texture upload, and a driver sync — even though it changes only on fades,
  flashes and movies. It's now guarded so it uploads only when the palette actually changes.
- **No per-turn CPU busy-spin.** The turn pacer busy-spun the tail of *every* game turn (a leftover Windows
  timer workaround), continuously burning a few percent of a CPU core and hurting laptop battery and
  thermals. The Linux path now sleeps precisely with a high-resolution monotonic timer — zero spin.
- **Faster sprite/text blit.** The core sprite- and glyph-drawing copy is now a single `memcpy` on its hot
  path (which the C library vectorizes with SIMD at runtime) instead of a byte-at-a-time loop through a
  double pointer.

These were found by a multi-agent Linux performance audit, are output-identical to the original, and are
verified in-game. (Two classes of change are deliberately held back: ones that would break
save/multiplayer/replay compatibility — the oversized core sim structs, combat target-search scaling — until
they can be proven safe under replay testing; and a wider `-march=x86-64-v2` build, which was tried and
reverted because GCC's auto-vectorizer miscompiles a loop in the 25-year-old pathfinding code. Correctness
first.)

**Stability & security fixes** *(genuine upstream bugs fixed locally, plus faults in this fork's own Linux layer; the security ones reported upstream)*
- **Files that came along for the ride are no longer treated as content.** Archives built on macOS or
  Windows carry a small hidden companion file beside every real one, and campaigns, mods and map packs
  reach a Linux install as archives. This fork's own directory scanner handed those companions to the game
  as content: it read them as campaigns, passed them to the graphics loader, counted them as levels, and
  told you on screen to install one as a mod. They also sort ahead of the file they shadow, so anything
  choosing by position chose the wrong one. Hidden files are now ignored everywhere the game looks for
  content, and the scanner has tests that run it against a real directory.
- **Creature-list corruption — found and fixed at the root.** Deleting a room never unlinked the
  creatures still working in it, and a creature leaving its room trusted that "no previous neighbour"
  meant it led the room's list. Together, one defeated keeper plus reclaimed territory let a dead room's
  worker chain get spliced into a healthy room — silent corruption planted thousands of turns before it
  aborted the game at a level transition. Rooms now sever their workers on deletion, the list head is
  only rewritten when the room agrees, and orphaned creatures detach themselves. The ~18 guarded list
  walks stay as the last line of defence, and the fault exists in upstream KeeperFX to this day.
- **Changing resolution no longer blacks out the game.** The GPU present layer maps the 8-bit
  picture through a colour table uploaded only when it changes — but rebuilding the backend on a
  resolution switch produced a fresh, empty table that the "unchanged" check then never refilled.
  Every pixel looked up black, permanently. The table is refilled on every backend rebuild.
- **Five memory faults found by our sanitizer pass** — the engine now builds under AddressSanitizer and
  every campaign is run through it before a release. Its first runs caught an array overflow on every
  game start, out-of-bounds reads on every level load and script parse, and a C++ destructor fault on
  every retired sound message — inherited faults dating from 2008 to 2025, all still present upstream.
- **Clean exit on quit** — avoids a shutdown segfault caused by the SDL3/Wayland teardown race on systems
  using `sdl2-compat`.
- **Ultrawide creature-possession crash** — the new C++ lens effect didn't size its buffer for large
  horizontal viewports; possessing a creature at 3440×1440 read past the buffer.
- **UTF-8 font heap-crash** — the new UTF-8 font loader freed a pointer into a *static* buffer, aborting on
  glibc the moment the generated `.fxfont` files exist.
- **Campaign crash on level start** — the `QUICK_MESSAGE`/`DISPLAY_MESSAGE` script commands corrupted the
  message index when given a chat-icon argument (a `ScriptValue` union overlap on 64-bit), crashing workshop
  campaigns like *Tempest Keeper* the moment their script ran. *(reported upstream:
  [dkfans/keeperfx#4969](https://github.com/dkfans/keeperfx/issues/4969))*
- **Hardened against crafted add-ons** — a multi-agent Linux audit found several out-of-bounds writes a
  malicious or buggy campaign/map/mod could trigger (script index checks, `.lif`/`.slx`/texture/palette
  file-load size bounds, PNG sprite & movie buffers) plus more union-aliasing and an unbounded WAV parse.
  All fixed. *(reported upstream:
  [#4970](https://github.com/dkfans/keeperfx/issues/4970))*
- **Audio on case-sensitive filesystems** — sound banks, music and movies now resolve the real on-disk
  filename case, so mixed-case files referenced by mods/campaigns actually play on Linux.

**Tooling**
- **Weekly upstream-sync bot** — a GitHub Action merges the team's latest `master`, compile-checks it, and
  opens a pull request for review (details [below](#how-the-tux-edition-stays-current-with-upstream)).
- **`refresh-alpha.sh`** — pulls upstream, builds with the correct version number, generates the UTF-8
  fonts, and deploys locally.
- **`make` builds Linux.** A bare `make` ran upstream's Windows/mingw target, which also overwrote a shared
  prebuilt dependency and left the *next* Linux build failing with a bare `cannot find -ljson`. It now
  builds Linux, and repairs that dependency automatically if a Windows build clobbered it.
- **`KFX_NONINTERACTIVE=1`** — startup problems (an unrecognised command-line option, for instance) raise a
  message box that waits for a click. Fine for a person, fatal for automation: CI, scripts and the
  launcher's extra-launch-options field all hang until something times out. With this set, the engine logs
  the message and carries on instead. Unset, nothing changes.

</details>

## 🤖 On AI-assisted development

**Yes, AI assistance is used here, heavily.** What people rightly call "AI slop" is *unverified output
pushed onto other people* — and none of that happens here: **no patches from this fork are pushed at the
KeeperFX team** (the weekly sync runs inbound only), and every line of the delta above is compiled and
**played on real Linux hardware** before it ships. A mistake here costs me a debugging session, never
someone else's review queue. **Judge the diff, not the toolchain** — it's all right there, one commit at a time.

<details>
<summary><b>The longer answer — what the strictest C projects actually do, with sources</b> &nbsp;<sub>(click to expand)</sub></summary>

The slop problem is real: curl [ended its six-year bug bounty](https://www.theregister.com/2026/01/21/curl_ends_bug_bounty/)
in January 2026 as valid reports collapsed, and the ["AI-DDoS"](https://arxiv.org/abs/2607.04003) on
maintainers is now a measured phenomenon — submissions that cost minutes to make and hours to disprove.

**Yet the most conservative C projects alive permit AI-assisted code, provided a human is accountable:**

| Project | Position |
|---|---|
| **Linux kernel** | [Permitted](https://docs.kernel.org/process/coding-assistants.html), tagged `Assisted-by:`. AI agents **must not** add `Signed-off-by` — only a human certifies the DCO and takes "full responsibility". |
| **LLVM** | [Permitted](https://llvm.org/docs/AIToolPolicy.html), human in the loop: "The contributor is always the author and is fully accountable." |
| **curl** | Ended its bounty over slop, yet still: "[We can accept code written with the help of AI](https://curl.se/dev/contribute.html), but the code must still follow coding standards…" |

Across 118 repos with a written AI policy, [78% permit it](https://arxiv.org/abs/2605.16706) and 74%
require a human in the loop. The rule is **accountability, not abstinence**.

**At scale it's measured, not hypothetical.** Google's year-long study: 595 changes,
[74.45% LLM-generated, ~50% less migration time](https://arxiv.org/abs/2504.09691); about
[75% of Google's new code](https://www.techspot.com/news/112152-google-ai-now-generates-75-new-code-up.html)
is now AI-generated and engineer-reviewed. Amazon upgraded
[30,000+ Java apps](https://aws.amazon.com/blogs/devops/amazon-q-developer-just-reached-a-260-million-dollar-milestone)
(~4,500 developer-years). [90% of developers](https://dora.dev/dora-report-2025/) use AI (DORA 2025).

**And the honest other half:** that same DORA report ties AI adoption to *higher* instability and calls it
an **amplifier** of whatever practice already exists, good or bad — while a
[METR trial](https://metr.org/blog/2025-07-10-early-2025-ai-experienced-os-dev-study/) found experienced
developers 19% *slower* with AI while feeling 20% faster. Both point the same way: the tool isn't the
variable that matters, the verification gate is. Hence the gate here — it compiles, it runs, I played it,
and I own the bug report. Found something sloppy or broken?
[Open an issue](https://github.com/ForkedInTime/keeperfx-linux-alpha/issues).

</details>

## 🚧 Work in progress

- **Truecolor GPU / Vulkan world renderer + hi-res asset pipeline.** Design docs, a frozen prototype
  world-renderer, and hi-res terrain/sprite plans live under [`docs/vulkan-foundation/`](docs/vulkan-foundation).
  The GPU display layer above is step one; the goal is a real truecolor, eventually 3D-capable renderer —
  developed slowly, on top of whatever the team ships, and always behind a separate switch so the classic
  look stays intact. **None of this is enabled in the current build.**

## System requirements

- **A current 64-bit Linux distro: Ubuntu 24.04 / 26.04 or newer, Fedora, Arch, Steam Deck**, etc.
- ⚠️ **Will NOT run on Ubuntu 22.04 or older.** Official builds are compiled on Ubuntu 24.04, whose C
  library (glibc 2.39) is newer than what 22.04 and earlier provide — the binaries simply won't start on
  them. Older systems must [build from source](#build-from-source) instead.
- An OpenGL 3.3-capable GPU.

## Other ways to install

<sub>On Arch or a derivative? Use the **[AUR package](#-arch-linux--install-from-the-aur)** above instead of
any of these.</sub>

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
   sudo pacman -S --needed sdl3 sdl3_mixer sdl3_image \
     ffmpeg openal luajit libspng minizip zlib libepoxy miniupnpc libnatpmp openssl zstd
   ```
5. Run: `cd ~/.local/share/keeperfx-alpha && ./keeperfx`
</details>

### Build from source

<details>
<summary>For developers who want to compile the engine themselves</summary>

Tested on Arch Linux (x86-64). Package **names** differ across distros, but the set is the same.

> **A source checkout is not a playable game, by design.** The repository tracks the engine and the
> campaigns' *scripts and configuration* — `campgns/keeporig` is 26 `.txt` files here, and 315 files
> in a release, the difference being the binary map data (`.slb`, `.own`, `.tng`, `.wib`, `.lgt`).
> That content comes from the upstream release payload during release assembly, so building from
> source gives you an engine, and you still need a release (or the launcher) for something to play.

**1. Install build dependencies** (Arch / derivatives):
```bash
sudo pacman -S --needed base-devel git python \
  sdl3 sdl3_mixer sdl3_image \
  ffmpeg openal luajit libspng minizip zlib libepoxy \
  miniupnpc libnatpmp openssl zstd
```
> Other distros: a C/C++ toolchain, `make`, `git`, `python3`, and the dev packages for SDL3 (+mixer/image),
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

## 🔧 Why this fork builds with `linux.mk`, not upstream's CMake

Upstream builds with CMake and we deliberately do not. This is the single biggest structural
difference between the two trees, so it is worth stating plainly rather than leaving people to
discover it in a build error.

Their CMake **does not build a working Linux target**, measured against the current tree rather
than remembered — upstream has improved it since this section was first written, and two of the
three problems it used to describe are gone:

- **It configures cleanly now.** SDL3 detection was fixed: it probes `sdl3-image` and `sdl3-mixer`,
  the names SDL actually ships, and falls back to the capitalised spellings. `cmake -S . -B build`
  completes in seconds with no `FetchContent` fallback. It also filters platform sources correctly
  in both directions, rather than only excluding a Linux file from Windows builds.
- **It still does not link.** Two libraries the engine calls are absent from its Linux dependency
  set: `libepoxy`, which every GL entry point in the OpenGL present backend resolves through, and
  `libswscale`, which `bflib_fmvids.cpp` calls for video scaling. A full build gets all the way to
  the link step and stops with **498 undefined `epoxy_gl*` symbols and 4 for `swscale`**, producing
  no binary. `linux.mk` links both.

So the conclusion is unchanged and the reason is now a single one: the dependency list is short by
two libraries. That is a much smaller gap than it was, and worth revisiting rather than treating as
permanent.

None of that is a criticism of upstream: KeeperFX is a Windows project, their CMake serves their
platform, and the Linux path in it is untested because nobody there runs it.

**It is exactly the reason this fork exists.** `linux.mk` is ours, it is what CI and the AppImage are
actually built with, and it is kept honest by every release. The price is small and paid knowingly:
when upstream adds source files we add them to `linux.mk` by hand — a few lines per refactor. We
consider that a better trade than adopting a build system that does not currently produce a working
Linux binary.

If upstream's CMake ever builds cleanly on Linux, this is worth revisiting; it would remove that
manual step. Until then, `linux.mk` is the supported way to build the Tux Edition.

## How the Tux Edition stays current with upstream

**A weekly sync bot does it automatically.** Every Monday a GitHub Action checks the KeeperFX team's
`master` for new commits and:

- **nothing new** → exits quietly;
- **merges cleanly and compiles** → opens a pull request into `alpha` listing every upstream change. Nothing
  merges itself: I read the diff before clicking **Merge** — closely where it touches the Linux path (engine
  internals, the SDL/OpenGL layer, the build, packaging), lightly where it's Windows-only churn that can
  never reach this build;
- **merge conflicts, or the build breaks** → opens an issue for manual attention instead.

<details>
<summary><b>More — curation policy, how releases are cut, and manual syncing</b></summary>
<br>

**We track upstream, but on our terms — we curate, we don't blindly fast-forward.** The bot automates the
routine merges; anything that conflicts (usually where our Linux-specific changes overlap a hot engine file)
is resolved by hand, at our discretion. We take upstream's improvements — *including when they supersede our
own*: e.g. their #5047 "Memset optimization" removed a wasteful per-frame buffer clear that we'd already
trimmed independently, so we adopted theirs and dropped ours — and we keep our Linux layer where it's the
better fit. The result is a fork that stays current with the team's work while remaining a deliberate,
reviewed selection rather than an automatic mirror.

**The Windows half doesn't come along for the ride.** Upstream builds for Windows — their CI cross-compiles
with MinGW and signs `.exe` patches — so those five workflows are deleted here and this repo's CI is
Linux-only. That's the standing rule, not a one-off tidy-up: a file that exists purely to build, sign or
ship the Windows product isn't applicable to this fork and doesn't get carried in, and a sync that tries to
restore one gets resolved by hand. What we deliberately *don't* do is rip
the Windows code paths out of the engine: they sit behind `#ifdef _WIN32` in a handful of files, cost this
build nothing, and tearing them out would mean re-fighting the same merge conflict every single week. Cut
the Windows *plumbing*, leave the shared *source* alone — that's what keeps the weekly merge cheap enough to
actually keep doing.

After the sync PR is merged, a release is cut, and publishing it is the whole job: CI builds the AppImage,
the game package (`full.7z`) and the portable tarball, and attaches all three to the release. Each build
layers the KeeperFX team's current data package over ours, so their new content arrives on its own rather
than waiting to be noticed. The Flatpak is rebuilt on a monthly schedule (it self-updates its game package
on launch, so it catches up in between). Existing installs are offered the new build by the launcher's
built-in updater.

The same sync can still be done by hand:
```bash
git fetch upstream                          # the KeeperFX team's master
git log --oneline HEAD..upstream/master     # see what they changed
git merge upstream/master                   # merge (resolve any conflicts)
./refresh-alpha.sh                          # rebuild and redeploy locally
```

</details>

## ❓ FAQ

### Can I switch between stable and alpha without losing my saved games?

Yes, and you can switch back. Your save files are never modified or deleted — but **a save opens
in the version that wrote it**, so they do not travel between channels.

In practice: a campaign tells you it needs the latest alpha, you switch to alpha, play it, then
switch back to stable — and your stable saves are exactly where you left them. The reverse is
equally true: progress you make on alpha stays on alpha, because stable cannot read it either.
What you cannot do is start a campaign on one channel and continue it on the other.

This is not a policy, it is the save format: a save is a raw copy of the engine's internal state,
so it only makes sense to an engine laid out the same way. When a save cannot be loaded the game
tells you so and returns you to the menu, and the engine that *can* read it is kept when you
update — so the campaign is a version switch away, not lost.

### How do I play a campaign my current version can no longer load?

Run the version that saved it. Updating keeps the previous engine automatically, three releases
deep, so it is already on your machine:

- **Arch (AUR)** — `keeperfx-tux-previous` lists what is kept and sets up an isolated copy of the
  game to play one. Its saves are separate from your current game and it cannot write to it.
- **AppImage / Flatpak** — the engines are kept beside your game directory, under `previous/`.

Nothing is copied out of your current install without being asked, and nothing you do there can
touch the campaigns you are playing now.

### How do I install DK1 on Linux?

KeeperFX needs your **own original *Dungeon Keeper* files** as proof of ownership — the launcher copies
them out of a real install. On Windows you'd just run GOG Galaxy; **there is no GOG Galaxy for Linux**, so
you install your GOG/Steam/EA copy with a Wine front-end first, then point the launcher at it.

<details>
<summary><b>Step-by-step — Lutris / Heroic / innoextract / Steam + Proton</b></summary>
<br>

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

**Option C — Extract the GOG offline installer (no Wine at all)**
If you have the GOG **offline installer** `.exe`, you don't even need to run it — unpack it directly:
```bash
sudo apt install innoextract        # Ubuntu/Debian (or your distro's package)
innoextract setup_dungeon_keeper_gold_*.exe
```
It dumps the game into an `app/` folder; point the launcher at that (its `data/` and `sound/` subfolders). This
is the lightest option — no GOG client, no Wine.

**Option D — Steam + Proton**
Own it on [Steam](https://store.steampowered.com/app/1996630/Dungeon_Keeper_Gold/)? Enable **Proton** for it
(Properties → Compatibility → *Force the use of a specific Steam Play compatibility tool*) and install — the
launcher detects the Steam copy.

**Then connect it to KeeperFX**

The KeeperFX launcher tries to **auto-detect** your install and copy the game files. If it can't find them,
it asks you to point it at the folder yourself — so locate the install inside the Wine prefix, typically:

- **Lutris:** `~/Games/dungeon-keeper/drive_c/GOG Games/Dungeon Keeper/` (or wherever you installed it)
- **Heroic:** `~/Games/Heroic/Dungeon Keeper/drive_c/...`

That folder (and its `data/` and `sound/` subfolders) is what the launcher copies from. You only do this
once; after that KeeperFX runs natively with **no Wine**.

> 💡 You don't keep using Lutris/Heroic to *play* — they're only there to install the original game so
> KeeperFX can borrow its data files. Once copied, launch KeeperFX directly.

</details>

## License

**GPL-2.0-or-later**, same as upstream KeeperFX — see [LICENSE](LICENSE). All upstream copyrights remain with
the KeeperFX team and contributors. The banner is adapted from the official KeeperFX banner (re-labelled for
this Linux alpha) with thanks. *Dungeon Keeper* is a trademark of its respective owners; this project is not
affiliated with them.
