# Changelog — KeeperFX Linux Alpha

This tracks the changes in *this* fork on top of the KeeperFX team's `master`.
Version numbers follow the engine build (`1.3.2.<build> alpha`).

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
