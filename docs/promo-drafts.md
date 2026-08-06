# KeeperFX Linux — promo drafts (Steam Guide + Steam review + GOG forum post)

Repo: https://github.com/ForkedInTime/keeperfx-linux-alpha
Latest AppImage: https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest

---

## 1) STEAM COMMUNITY GUIDE
(Dungeon Keeper Gold hub → Community → Guides → Create Guide)

**Title:** Play Dungeon Keeper natively on Linux & Steam Deck (KeeperFX)

**Short description:** A one-file, no-Wine way to run Dungeon Keeper on Linux and the Steam Deck, using the community KeeperFX engine. Bring your own Steam copy — this just plays it natively.

---

### What this is
KeeperFX is a beloved community remake/patch of *Dungeon Keeper 1* — bug fixes, high resolutions, quality-of-life, decades of work by the KeeperFX team. It's officially **Windows-only**. This guide covers an **unofficial, community-built native Linux port** of it, so Linux and Steam Deck players can run it with **no Wine, no Proton, no compatibility layer** — a single AppImage.

> Unofficial and not affiliated with the KeeperFX team or EA. All the game engine credit goes to the KeeperFX team (https://keeperfx.net). You supply your own legally-owned Dungeon Keeper Gold — which, if you're reading this on its Steam page, you already have. 🙂

### You need
- Dungeon Keeper Gold (this Steam page) installed once, so its data files exist on disk.
- A current 64-bit Linux distro **or** a Steam Deck.

### Steps (Desktop / Linux)
1. **Install DK Gold once** so its files are on disk. On Linux, enable Proton for it: *Properties → Compatibility → Force the use of a specific Steam Play compatibility tool → Proton*, then Install.
2. **Download the Linux build** (one file):
   https://github.com/ForkedInTime/keeperfx-linux-alpha/releases/latest/download/KeeperFX-Linux-Alpha-x86_64.AppImage
3. Make it runnable and launch it:
   ```
   chmod +x KeeperFX-Linux-Alpha-x86_64.AppImage
   ./KeeperFX-Linux-Alpha-x86_64.AppImage
   ```
4. The launcher opens, **auto-detects your Steam copy** of DK Gold, and copies the game data it needs. If it can't find it, point it at your Steam install folder
   (`.../steamapps/common/Dungeon Keeper Gold`).
5. Set your graphics/sound and hit Play. That's it — native, GPU-accelerated, no Wine.

### Steam Deck notes
- Do the above in **Desktop Mode** (hold Power → Switch to Desktop).
- If the AppImage complains about `libfuse.so.2`, run it without FUSE:
  ```
  ./KeeperFX-Linux-Alpha-x86_64.AppImage --appimage-extract-and-run
  ```
- To play from **Gaming Mode**: in Desktop Mode, right-click the AppImage → *Add to Steam* (or Steam → Add a Non-Steam Game), then launch it from your Library like any game. Controller/touch work through the desktop session.

### Don't have DK on Steam?
Any legit copy works — GOG or an old CD. See the project's README for GOG/Lutris/Heroic/innoextract instructions.

### Links
- Download & full README: https://github.com/ForkedInTime/keeperfx-linux-alpha
- Upstream KeeperFX (the actual game, Windows): https://keeperfx.net

Enjoy, Keepers. 🐧👑

---

## 2) STEAM REVIEW
(Recommended — keep it about the game, point to the guide)

Timeless. Being the villain never gets old — build your dungeon, hoard gold, and slap imps into working harder. The narrator alone is worth it.

Bonus for penguins: there's a **native Linux / Steam Deck** way to play it through the community **KeeperFX** engine — no Wine, no Proton, one file, and it uses this Steam copy's data. I wrote a Community Guide with the steps: check the Guides tab on this hub ("Play Dungeon Keeper natively on Linux & Steam Deck").

(Unofficial community port; all engine credit to the KeeperFX team. Bring your own game — which you just did.)

---

## 3) GOG COMMUNITY FORUM POST
(https://www.gog.com/forum/dungeon_keeper_series — new thread)

**Subject:** Native Linux build of KeeperFX (unofficial community fork) — for Linux & Steam Deck players

Hey all —

KeeperFX is fantastic but ships Windows-only, so I put together an **unofficial, native Linux build** of it for those of us on Linux and the Steam Deck. No Wine, no Proton — a single AppImage that runs the engine natively and uses your own DK Gold files (GOG copy works great).

- Download & instructions: https://github.com/ForkedInTime/keeperfx-linux-alpha
- One file: download the AppImage, `chmod +x`, run. The launcher auto-detects your GOG/Steam/CD install and copies what it needs.
- GOG users: if the launcher doesn't auto-find it, point it at your installed DK folder (or unpack the GOG offline installer with `innoextract` — no Wine needed).

To be clear: this is **unofficial and not affiliated with the KeeperFX team or GOG** — all the engine credit belongs to the KeeperFX team (https://keeperfx.net). I just maintain the Linux port and re-sync it with their latest work. Bug reports for the Linux build go to my repo, not theirs.

Hope it helps some fellow Keepers get their dungeons running on Linux. 🐧
