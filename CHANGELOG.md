# Changelog — KeeperFX Linux Alpha

This tracks the changes in *this* fork on top of the KeeperFX team's `master`.
Version numbers follow the engine build (`<major>.<minor>.<release>.<build> alpha`).

## 1.4.0.5322 — 2026-07-29
- **Crash hardening — creature-list corruption is now survivable.** A corrupted creature-list link (a
  "next" pointer aimed at a slot that isn't a creature) could crash the whole game with an abort — e.g.
  when advancing between campaign levels after the creature list got tangled mid-level (visible symptom:
  a creature resting away from its lair; log shows *"Jump to invalid creature N detected"*). The engine
  now validates each creature in a list before using it, in the ~18 walks that previously lacked that
  check; on a bad link it logs rich diagnostics (turn, list, slot, thing type) and safely stops that walk
  instead of aborting — the game keeps playing (at worst one glitched creature is skipped that frame).
  Purely additive guards, mirroring the ~50 the engine already had. This is a genuine engine bug present
  upstream too; the added logging is the path to a permanent root-cause fix if it recurs.

## 1.4.0.5321 — 2026-07-29
- **In-launcher Workshop browser (new).** A **Browse Workshop** button opens the full
  [keeperfx.net workshop](https://keeperfx.net/workshop) catalogue *inside* the launcher — search, filter by
  category, sort by rating / downloads / newest, thumbnails, and a **Details** link to each item's page — with
  **one-click install** for maps, campaigns, map packs and mods. A new **Installed** tab scans your add-ons,
  labels each as 🏰 KeeperFX-stock or 🌐 Workshop/user (filterable by category + name), and offers a
  **reversible Uninstall → Restore**: removed items move to a local backup you can Restore in one click or purge.
- **Standalone maps + cleaner installs.** Single workshop maps now install correctly (into `levels/personal`),
  and version-control / OS junk (`.git`, `__MACOSX`, `.DS_Store`) is no longer copied out of archives.
- **Single-instance launcher.** The launcher refuses to open a second copy (two launchers could start two
  games fighting over the same saves); a hidden `--allow-multiple` flag overrides it for development.
- **Clearer update errors + window focus.** When a release has no Linux package yet, the updater explains that
  instead of a bare "Archive download failed"; the launcher window now raises/activates itself on show.

_Engine is unchanged from 5320 — this is a launcher-focused release._

## 1.4.0.5319 — 2026-07-27
- **Weekly upstream sync (manual merge).** Folded in the KeeperFX team's latest 9 patches (#5039–#5062).
  The auto-sync bot correctly flagged a merge conflict in `engine_render.c` for human resolution rather than
  forcing a bad merge; resolved by hand and build-verified on Linux. Highlights:
  - **Rendering fixes** — big sprites no longer clipped by floor tiles in straight view (#5062); the Dungeon
    Heart no longer clips when zoomed in straight view (#5060); the landview (map) overlay no longer drops an
    edge pixel at high resolution (#5058). All three benefit our high-res Linux setups.
  - **UTF-8 text input** (#5039): accented characters (ñ, ç, é, ¿¡ …) can now be typed in save names, chat,
    and other text fields.
  - **Mapmaker/modding:** new `COPY_CREATURE_TYPE` script command (#5050); maps can load string subtypes in
    `.tngfx` files (#5052).
- **Converged with upstream on the poly-pool clear.** Our 5305 Linux perf pass had trimmed the renderer's
  per-frame 16 MB poly-pool `memset` down to just the high-water-mark region; upstream's #5047 ("Memset
  optimization") then removed it **entirely** — the fuller form of the very insight our own analysis reached
  (the bucket-heads clear is what guarantees correctness; the pool clear was always redundant). We adopted
  upstream's version and dropped our now-superseded code. Same result, one source of truth, and this hot file
  no longer re-conflicts on every sync.

## 1.4.0.5305 — 2026-07-20
- **Linux engine performance pass.** Four output-identical optimizations to the native Linux build,
  found by a multi-agent performance audit and verified in-game across multiple levels:
  - **Renderer no longer zeroes a 16 MB buffer every frame.** The isometric view cleared its entire
    polygon pool each frame regardless of use; it now clears only the region actually touched.
  - **The GPU no longer re-uploads the palette every frame.** The OpenGL present path re-sent the
    256-colour palette (CPU expand + texture upload + driver sync) every frame; it's now guarded to
    upload only when the palette actually changes (fades/flashes/movies still work).
  - **No per-turn CPU busy-spin.** The turn pacer busy-spun the tail of every game turn (a leftover
    Windows timer workaround); the Linux path now sleeps precisely with a high-resolution timer,
    lowering idle CPU and helping laptop battery/thermals.
  - **Faster sprite/text blit.** The core sprite/glyph copy is now a single (SIMD-vectorized) `memcpy`
    instead of a byte-at-a-time loop.
- *A wider `-march=x86-64-v2` build was tried and reverted:* GCC's auto-vectorizer miscompiled a loop in
  the 25-year-old pathfinding code, crashing on level start. Correctness first — the four wins above are
  independent of it.

## 1.4.0.5296 — 2026-07-20
- **Merged the KeeperFX team's latest alpha patches** (weekly upstream-sync bot, 19 commits,
  #5019–#5049). Verified to build clean on Linux before release. Highlights:
  - Engine performance: `gpoly` polygon rasterizer optimized for ~10% faster rendering (#5043)
    and map-parchment (minimap) redraw performance fixed (#5041) — both benefit single-player
  - New feature: **map-specific sounds and speeches** — maps can ship custom sounds/speech in
    their `.zip` bundle (#5019), backed by a new shared `custom_zip` reader factored out of the
    sprite loader
  - Gameplay/engine fixes: menu-sound gating fixed (#5028), mouse-light flicker on GUI menus
    fixed (#5040), highscore input caret scaled/shaded/framed correctly (#5044), computer
    players no longer sell enemy traps when building rooms (#5034), `pngpal2raw` tool build
    error fixed (#5035)
  - Maps: **Fortress** added, Bash neutered, Dread Mountain fixed (#5032)
  - Controller rumble now only fires when a controller was the last input used (#5049)
  - Multiplayer: reliable slap/power-click (#5026), version-mismatch message on start (#5025),
    client clock synced to host packet-receive time (#5031), dynamic input-lag adjustment
    (#5033, #5036), desync fixes + logging (#5042), possible ghost-tagging fix (#5030), fixed
    picking up and casting at the same time (#5038), client-side info buttons (#5045)

## 1.4.0.5273 — 2026-07-15
- **Fixed: game config was frozen in the update payload.** The release pipeline built the
  `full.7z`/AppImage payload from the previous release and only refreshed the engine binary,
  fonts, and launcher — never the game config (`fxdata`/`creatrs`/`mods`). So config changes
  never reached players even as the engine advanced. This shipped a stale `objects.cfg` in
  5272 that **broke slapping chickens** (the new engine gates object-slapping on a `SLAPPABLE`
  flag the frozen config lacked) and, via the mismatched hand/object code, caused a
  **heap-corruption crash on the level-victory transition**. The packaging now overlays the
  freshly-built engine config onto the payload, matching the portable-tarball build. Restores
  chicken-slapping and fixes the crash; no engine code change.

## 1.4.0.5272 — 2026-07-15
- **Merged the KeeperFX team's latest alpha patches** (weekly upstream-sync bot, 22 commits).
  Highlights:
  - Engine performance: path-finding map generation optimized and split out into
    `ariadne_update.c` (#5012), the ariadne navigation module refactored (#5004), and room
    recalculations optimized (#5002)
  - Camera/input: rotate the camera around the mouse position (#4898), Delete/PageDown default
    to rotate/zoom again (#5010), fixed hand-of-evil animation issues (#5011)
  - Gameplay fixes: magic boxes no longer unclickable (#5001), GUI pickup no longer slightly
    slow (#5009), fixed double sound fade-out on the landview ensign (#4993), campaign creature
    lists now replace rather than append (#4999)
  - Lua: new OnPickup and OnSlap triggers (#4805), fixed ACTIVATIONLUAFUNC (#5015)
  - Continues are now compatible between future versions (#5014)
  - Translated error messages across all languages (#4997)
  - Multiplayer: local-camera tagging (#5018), resync console command (#5005) + lua-data resync
    (#4966), reliable creature clicking (#5000), stutter tracking (#5006), hand-of-evil fixes
    (#5003), clean disconnect after failed startup (#5008), FRAMES_PER_SECOND limit re-enabled
    (#4994)

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
