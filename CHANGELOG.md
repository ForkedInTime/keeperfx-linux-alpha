# Changelog — KeeperFX Linux Alpha

This tracks the changes in *this* fork on top of the KeeperFX team's `master`.
Version numbers follow the engine build (`<major>.<minor>.<release>.<build> alpha`).

## 1.4.0.5246 — 2026-07-08
- **Merged the KeeperFX team's latest alpha patches** (first pull request from the weekly
  upstream-sync bot). Upstream bumped the base version to **1.4.0**. Highlights:
  - Improved frame interpolation (#4919) and fixed/improved fast-forward frame skip (#4989)
  - Multiplayer: keep lobbies alive after going to the landview screen (#4981), close
    lobbies faster (#4982), fixed network crash 449 (#4992), fixed workshop-item bounding
    boxes turning invisible (#4991)
  - Two new 2v2 multiplayer maps (#4987)
  - Slappable objects (#4894)
  - Lua: creature transform function (#4985) and max level for new creatures (#4978)
  - Music loading for mods (#4952)
  - Fixed SDL compile race / library targets (#4983, #4976), missing return in
    SET_BOX_TOOLTIP (#4979), removed unused Name fields in configs (#4980), in-game error
    message for misplaced zip files (#4995)

## 1.3.2.5224 — 2026-07-01
- **Merged the KeeperFX team's latest alpha patches:**
  - Native Linux build works on modern GCC (#4962)
  - Fix lobby chat (#4967) and disable unsynced multiplayer lua date/time functions (#4958)
  - Legacy Disease hurt time 400→144 (#4968)
  - Actionable things flash under the cursor again when using cheats (#4977)
  - Fix modded creatures refusing to train (#4975)

## 1.3.2.5216 — 2026-06-30
- **Case-insensitive paths now resolve full directories,** not just the filename — so a
  mod/campaign that references e.g. `mods/MyMod/sound/Foo.wav` still loads when the real
  folders are lower-case. (Exact-case paths are unchanged — purely a fallback.)
- **Crash handler is async-signal-safe:** it no longer tears down the screen/GL/SDL from
  inside the signal handler (that could deadlock when the fault was in the GL driver or the
  allocator and suppress the crash report); the signal info + backtrace are still written.

## 1.3.2.5215 — 2026-06-30
- **More audit fixes (correctness & robustness).** Following the security pass: fixed
  several `ScriptValue` union-aliasing bugs (wrong objective/information popup location,
  wrong `move_creature` target, wrong `add_object` angle), added missing range-check
  returns in `USE_POWER_ON_CREATURE`, corrected `%I64d` formats and a `(bpp = 32)`
  assignment bug, hardened slabset config parsing, and stopped a malformed WAV from
  hanging the loader.
- **Audio works on case-sensitive filesystems.** Sound banks, music, streamed samples and
  movies now resolve the real on-disk filename case, so mixed-case files referenced by
  mods/campaigns play on Linux instead of silently failing.
- **Launcher: Install… no longer overwrites stock files.** Installing a campaign/mod/map
  pack keeps existing files (stock campaigns, other add-ons) instead of clobbering them.

## 1.3.2.5213 — 2026-06-30
- **Security/stability — hardened against crafted add-ons:** a multi-agent Linux audit
  found several out-of-bounds writes a malicious or buggy workshop campaign/map/mod could
  trigger. Fixed: `SET_BOX_TOOLTIP` and `SET_POWER_CONFIGURATION` script-index checks;
  `.lif`/`.slx`/texture/palette file loads now bound the read to the buffer (RNC-unpacked
  size); PNG sprite RLE buffer sizing; and the CPU movie path skips oversized frames.
  Reported upstream (dkfans/keeperfx#4970).
- **Launcher:** the Map Editor (Unearth) and the clickable workshop/news tiles now launch
  with a clean environment, so they work from the AppImage like the other buttons.

## 1.3.2.5211 — 2026-06-30
- **Fixed a campaign crash (segfault):** the `QUICK_MESSAGE` and `DISPLAY_MESSAGE`
  script commands corrupted the message index when given a chat-icon/player argument
  (a `ScriptValue` union overlap), causing an out-of-bounds read on level start. This
  crashed workshop campaigns such as **Tempest Keeper** and **Another Dungeon** the
  moment their level script ran. Reported upstream (dkfans/keeperfx #4969).

## 1.3.2.5209 — 2026-06-30
- **Universal "Install…" in the Mod Manager:** one button now installs any KeeperFX
  add-on from a `.7z`/`.zip` — mods, **campaigns**, and map packs — putting each in
  the right place (campaigns appear in the in-game Land selection). Replaces the old
  mod-only "Install mod…", which mis-filed campaigns as mods.
- **Fixed the Workshop / Discord / Website / Open-folder / Open-log buttons** doing
  nothing under the AppImage: they now launch your browser/file manager with a clean
  environment instead of the bundled one.
- **Faster release CI:** AppImage builds are cached (ccache + Qt + deps); the Flatpak
  moved to its own monthly/manual workflow.

## 1.3.2.5208 — 2026-06-30
- **Install mods from the launcher:** the Mod Manager has an **Install mod…**
  button — pick a `.7z`/`.zip` (e.g. a keeperfx.net workshop download) and it
  extracts it into your `mods/` folder, generating a `mod.cfg` if the archive
  lacks one, then refreshes the list. No more hand-extracting into the install dir.
- **Launcher size — "Comfortable (110%)":** a smaller step between Normal (100%)
  and Large (125%), so the UI scale jump isn't so big.
- **Mod thumbnails:** mods without their own picture now show the KeeperFX icon as
  a placeholder instead of an empty frame.

## 1.3.2.5207 — 2026-06-30
- **In-launcher Mod Manager:** the launcher now lists the mods in your install's
  `mods/` folder with an Enabled toggle that reads and rewrites `load_order.cfg`,
  so you can turn mods on/off without hand-editing the file. (Section grouping and
  drag-to-reorder are planned next.)
- **Launcher updates ship in `full.7z`:** the AppImage CI now packages the same
  flat payload it builds — including the freshly-built launcher — as the update
  archive, so launcher fixes reach existing installs through **Update**, not only
  through a fresh AppImage download.

## 1.3.2.5206 — 2026-06-29
- **Accessibility — "Launcher size":** a new Settings → Launcher dropdown scales
  the whole launcher UI (Normal / Large / Larger / Huge / Largest, 100–200%) for
  players with low vision. Persists, and applies on the next launch.

## 1.3.2.5205 — 2026-06-28
- **Upstream alpha patches merged** (KeeperFX `master`, patches 5177–5179):
  - Fix tooltip boxes being too big again (#4959)
  - Fixed creatures not colliding with other things (#4955)
  - Fixed swap creature on `map.creature` again (#4960)

## 1.3.2.5200 — 2026-06-28
- **Self-updating launcher:** the launcher is now part of the update package, so
  in-launcher updates refresh the **launcher itself** — not just the engine and
  game data. Earlier builds only updated the engine, so launcher fixes (like the
  hidden download-server picker) needed a fresh AppImage download. The AppImage no
  longer overwrites a launcher that an update has upgraded.
- **Update package is flat:** the `full.7z` now unpacks files at the install root
  instead of a nested folder, fixing an update *loop* where the version never
  "stuck" and the launcher re-downloaded the same release on every launch.

## 1.3.2.5199 — 2026-06-28
- **Distribution:** one-file **AppImage** and **Flatpak** for any current 64-bit
  Linux distro (built on Ubuntu 24.04 via CI, runs everywhere — Ubuntu, Fedora,
  Arch, Steam Deck). Each bundles the engine, every library, and the game data;
  the only thing you provide is your own *Dungeon Keeper* files.
- **Launcher (Linux-native):** runs the engine directly (no Wine); reads the real
  version; installs/updates from this repo's GitHub releases; adds an applications
  **menu entry** on first run.
- **DK auto-detect:** the launcher now *searches* the disk (Wine/Lutris/Heroic/
  Steam/GOG/mounts) for your Dungeon Keeper files, instead of only checking fixed
  paths — and tells you clearly if it can't find them.
- **In-launcher updates:** "Update available — vX" / "No updates at this time"
  against this repo's releases.
- **UI tidy:** hide the keeperfx.net/Cloudflare download-server picker (we use
  GitHub); quiet harmless AppImage log noise (Wayland plugin / gvfs module).
- **Engine extras (carried from earlier):** GPU OpenGL-3.3 display path, truecolor
  front-end movies, and fixes for clean exit, ultrawide creature-possession, and
  the UTF-8 font crash.

See the full project description in [README.md](README.md). The truecolor/Vulkan
3D renderer remains **work in progress** (not enabled).
