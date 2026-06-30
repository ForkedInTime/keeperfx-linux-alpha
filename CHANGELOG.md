# Changelog — KeeperFX Linux Alpha

This tracks the changes in *this* fork on top of the KeeperFX team's `master`.
Version numbers follow the engine build (`1.3.2.<build> alpha`).

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
