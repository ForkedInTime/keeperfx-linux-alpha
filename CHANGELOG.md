# Changelog — KeeperFX Linux Alpha

This tracks the changes in *this* fork on top of the KeeperFX team's `master`.
Version numbers follow the engine build (`<major>.<minor>.<release>.<build>`), with
`alpha` appended on the alpha channel and nothing appended on the stable one.

## 1.4.0.5485 — 2026-08-10 — alpha

**Four memory faults fixed, found by turning on the compiler's own alarms.** The engine
now has a memory-checking build (AddressSanitizer) and a script that runs every installed
campaign under it; its first afternoon caught four faults that months of play never
showed, all inherited from the original project and all present since well before this
fork's first release.

- **The colour table wrote past its own end on every game start.** One array held ten
  entries while the code filling it wrote eleven — it has been like that since the
  purple, orange and black player colours were added. The stray write landed on the
  dungeon heart's beat-animation state, so the practical symptom was at most a heartbeat
  wobble; the fix sizes the array from the same constant the filler uses, so the two can
  never drift apart again.
- **The level-script reader looked one letter past the end of every command name** shorter
  than the word it was checking, on nearly every campaign. Read-only and harmless in
  practice, now simply correct.
- **Tiny map files were read past their end on every level load.** Some map files are
  legitimately a single byte; the decompressor read an eighteen-byte header out of them
  regardless. Files too small to be compressed are now treated as what they are.
- **Finished sound messages were destroyed as the wrong type** — a one-line C++ mistake
  (a missing virtual destructor) that made every retired message undefined behaviour.

None of these was dangerous to your saves, but all four were real, and the tooling that
found them is now part of the project: a build mode any developer can run, a weekly
check that keeps it working, and a compiler setting that turns an entire class of bug —
a failure result silently ignored — into a build error from now on.

## 1.4.0.5482 — 2026-08-10 — alpha

**Upstream sync.** Three changes from the KeeperFX team, one of which needed care.

- **Creatures that were asleep on payday now get the gold you handed them.** An advance
  taken while a creature was in a non-salary state — sleeping, most often — did not take
  effect until later. Upstream's fix, taken as-is.
- **File enumeration and OS-version queries moved into the platform layer**, continuing the
  rework this fork already adopted. Both are internal tidying you will not see.
- **Kept: hidden files stay invisible to the game.** Upstream's new enumeration skips only
  `.` and `..`, where this fork skips every hidden entry. That matters on Linux: campaigns,
  mods and workshop content usually arrive as archives built on macOS or Windows, which
  carry `._name.cfg` sidecar files beside the real ones. Without the filter the game reads
  those as real campaigns and mods — and because `.` sorts first, the sidecar is picked
  ahead of the file it shadows. The filter was carried into the new code; verified by
  planting sidecars and confirming the engine ignores them.

## 1.4.0.5475 — 2026-08-10 — alpha

**Changing resolution no longer blacks out the game.** This one also affects the current
stable release, 1.4.0.5423.

- **Fixed: the screen went black, permanently, after changing resolution in the in-game
  Graphics menu.** The game kept running — it was still drawing and presenting frames at
  full speed, they were simply black — and nothing brought it back: no further resolution
  change, no key, no menu. The 8-bit picture is turned into colour on the GPU using a
  256-colour lookup table. Uploading that table every frame was wasted work, since it only
  changes on fades, flashes and movies, so it is uploaded only when it differs from the last
  one sent. Changing resolution rebuilds the graphics backend, which creates a fresh, empty
  lookup table — but the game's own palette either side of that rebuild is identical, so the
  check concluded there was nothing to send. Every pixel then looked itself up in a table
  full of zeroes, which is black, and it stayed black because the palette never genuinely
  changed again. The table is now refilled whenever the backend is rebuilt. Switching
  between borderless fullscreen, exclusive fullscreen and your own windowed resolutions all
  work again.

## 1.4.0.5473 — 2026-08-09 — alpha

**Follow-up to the save delete button.** Engine-side; the launcher is unchanged.

- **The delete button explains itself.** Hovering the skull now reads *Delete this saved
  game*, in all sixteen languages the game ships. The wording follows the engine's own
  tooltip convention of a short heading before a colon — that is not cosmetic: the tooltip
  box is sized to whatever comes before the colon, and without one the box sizes itself to
  the whole sentence and runs off the right edge of the screen, since these buttons sit in
  the menu's right margin.
- **Clicking away from a name you are typing no longer freezes the menu.** Clicking a save
  row starts editing its name, and while any text field is being edited the game gates out
  every button on screen. So clicking a row and then changing your mind left the menu
  unresponsive with nothing to explain why, and clicking again did not help — only Enter or
  Escape did. This has always been the case; the delete buttons simply made it easy to run
  into, because there was previously nothing else on that menu to click. A click outside the
  field now ends the edit and still reaches the button you aimed at, in one click. What you
  had typed is kept but not saved, so nothing is committed and nothing is thrown away. This
  applies to every text field in the game, not only the save menu.

## 1.4.0.5470 — 2026-08-09 — alpha

**You can delete a saved game.** Engine-side; the launcher is unchanged from 1.4.0.5467.

- **A delete button on every save slot.** There are eight save slots, and until now the only
  way to reclaim one was to overwrite it. That is fine until you are mid-campaign and the
  names of the games you saved weeks ago no longer mean anything — you have to overwrite
  something without knowing what you are destroying. Nothing in the engine deleted a saved
  game at all. Each row on the in-game Save and Load menus now carries a skull button in
  the right margin, using Bullfrog's own "dead keeper" symbol. It appears only on slots that
  hold a game, and it asks before doing anything. The confirmation **names the save it is
  about to delete**: the dialog covers the list, and telling apart saves you can no longer
  identify is the whole point. Confirm and the slot frees, the row reverts to `UNUSED` and
  the menu stays open, so you can clear a couple of slots and save without leaving for the
  dungeon and coming back.

## 1.4.0.5467 — 2026-08-08 — alpha

**A launcher fix for the map editor.** The engine is unchanged from 1.4.0.5465.

- **The map editor is kept up to date.** The launcher used to install Unearth once and
  then only ever check that it was still there, so whichever version you first installed
  is the one you kept — indefinitely. That mattered more than it sounds: an install from
  before the start of July predates the release where Unearth learned to find a native
  Linux KeeperFX. Until then its file picker looked only for `keeperfx.exe`, so on Linux
  it could not see the game at all, and nothing would ever have offered you the version
  that fixes it. The launcher now remembers which release it installed and offers the
  newer one when you open the editor.

## 1.4.0.5465 — 2026-08-08 — alpha

**Mostly the launcher this time.** The engine gains upstream's windowing rework and
little else; everything you are likely to notice is in the launcher.

- **Your Tux Edition releases now appear in the launcher.** The "Latest News" panel reads
  keeperfx.net, which reports the Windows project's releases — so this launcher was
  updating you to builds it never mentioned. The newest Tux Edition releases now sit at the
  top of that panel, above the KeeperFX team's news, which is still there: their engine
  changes reach you too.
- **You can read the logs without leaving the launcher.** Play ▾ → **View logs** opens the
  launcher log and the game log side by side, with a Copy button — the usual next step when
  reporting a problem — and it keeps following the file while it is open, so you can
  reproduce a fault and watch it happen. When something fails, the error itself now offers
  a **Show log** button.
- **Workshop items that the site publishes but its API does not are installable again.**
  Some items — those needing an unreleased game version — are served perfectly well by
  keeperfx.net while its API reports no file for them at all. The launcher used to say
  "Could not get a download link", which reads like a fault at our end. It now falls back
  to the item's own page. Where genuinely nothing is published, it says so, and says where
  the gap is.
- **The window resizes properly.** It was already draggable on Wayland — the code that
  tried to prevent it has no effect there — but nothing moved when you did, so it only
  looked broken. Now the workshop panel lays its cards out across as many columns as fit,
  and the window opens wide enough to show four of them in a row.

And in the engine:

- **Upstream's windowing and platform rework**, taken so this fork does not drift from a
  file both projects edit heavily. It cost us nothing you can see: the OpenGL support their
  version had no way to express is carried on top of it, and a fault it reintroduced — the
  DESKTOP_FULL video mode quietly losing its real mode switch — is fixed again.
- **Multiplayer and screenshots on SDL3**, from the KeeperFX team.

## 1.4.0.5459 — 2026-08-08 — alpha

**The engine now runs on SDL3.** The KeeperFX team moved off SDL2, and this brings that
across. SDL is the layer between the game and your desktop — window, input, sound — so
this touches everything, which is why it lands on the alpha channel first and not on
stable. Nothing about it should be visible to you: the same resolutions, the same
controls, the same audio. If anything behaves differently from 1.4.0.5423, that is worth
reporting, because it is not intended.

Five faults were found reviewing the port before it shipped. Two of them fail silently,
which is the kind that otherwise reaches you and gets blamed on something else:

- **Winning a level could crash the game.** The cutscene where the Lord is dragged to the
  torture chamber was missing from the frontend's state handling, so completing a level
  that unlocks it could take the game down instead. That fault has been there since the
  sequence was added in 2019.
- **Slow mouse movement was being thrown away.** SDL3 reports pointer motion in
  fractions of a pixel where SDL2 used whole numbers, and the fractions were being
  discarded rather than accumulated. On Wayland, on a HiDPI screen, or with pointer
  acceleration, a slow drag could register as no movement at all.
- **"DESKTOP_FULL" had quietly stopped being a real fullscreen mode.** It had become
  identical to "DESKTOP" — a borderless window — so choosing it no longer changed the
  display mode as it says it does.
- **Leaving fullscreen could strand the window at the wrong size,** because SDL3 applies
  that change in the background where SDL2 applied it immediately, and the game resized
  the window before the change had happened.
- **Music could go silent for the rest of the session.** If a track failed to start, the
  previously playing track had already been discarded, and every later request to play it
  again was treated as already-playing and did nothing.

Also in, from the KeeperFX team:

- **Hand rules now apply to prisoners,** not only to your own creatures.
- **The dig-tagging sound always plays in multiplayer,** rather than being swallowed when
  the game was predicting your dig locally.
- **A possible crash in the drag animation** on the frontend.
- **Creature names are now written as comments in the configuration files,** which makes
  them far easier to edit by hand.

Behind the scenes, the release pipeline builds SDL3 from source rather than moving to a
newer Ubuntu, so the AppImage keeps running on every distribution it ran on before.

## 1.4.0.5423 — 2026-08-05 — stable

**Replaces 1.4.0.5411 as the stable release.** The engine is unchanged; all of this is
the launcher. A review of it after 5411 went out found thirteen faults, and the ones that
mattered were in the two things people do most: updating, and installing add-ons.

- **Add-ons from the Workshop install again.** Every file whose name contains a space —
  a large share of the catalogue — failed to download, because the site hands out a link
  its own server rejects. And a campaign, which ships as a bare `name.cfg` beside a
  `name/` folder, was not recognised at all: it was mistaken for a loose map pack, so its
  levels were scattered into Personal Levels, its campaign file and creature, land and
  media folders were dropped, and it reported success. Campaigns, map packs, single maps,
  creature mods and graphics packs were each tested against a real download from the
  Workshop, and each now lands where it belongs.
- **Where an add-on goes is read from the add-on itself.** Its configuration says whether
  it is a campaign or a map pack, which the folder layout cannot tell apart — and which
  the Workshop's own category sometimes gets wrong.
- **A failed install says so.** Copying into a folder it could not write to reported a
  successful install and wrote nothing. On an Arch install the game's data folders belong
  to the package manager, so that was every add-on install. Those folders now allow your
  own content alongside the packaged content, and a genuine failure explains itself.
- **Uninstall and Restore no longer fail silently — and Restore cannot lose your add-on.**
  It moved files back, ignored whether the move worked, then deleted the backup and its
  record regardless. If it could not write, the add-on was gone from the game and gone
  from the backup. The backup is now kept whenever anything fails.
- **A damaged download is noticed.** The check for a corrupt archive could never fire, so
  a truncated file went straight to being unpacked.
- **Opening Settings with four or more resolutions configured is no longer undefined.**
- **When something fails, it tells you why.** "Could not download" now includes the reason.

## 1.4.0.5411 — 2026-08-05 — first stable

**The first stable release of the Tux Edition.** Everything here has already shipped
through the alpha channel; what is new is the promise. The alpha channel keeps moving —
the SDL3 migration is being prepared there now — while stable stays put until a release
has been played and left alone for a while.

- **Same engine as 1.4.0.5397**, which had been soaked in ordinary play with nothing to
  report, and which starts and runs every one of the 13 installed campaigns — checked one
  at a time, each on its own first level.
- **Two update channels, kept apart.** The launcher now asks for the newest release *of
  its own channel* rather than the newest release overall. Choose stable and alphas will
  not be offered to you; choose alpha and you keep getting them. Previously there was one
  list and whichever release was newest won, which would have made a stable channel
  meaningless the moment the next alpha appeared.
- **Stable releases keep their build number.** A stable was being read as `1.4.0`, dropping
  the part that actually identifies the build, so an install compared older than the very
  release it was running and was offered an endless update to itself. Version ordering was
  also wrong in a way that could have offered a downgrade: a large build number on an older
  base counted as newer.
- **The launcher no longer offers updates it cannot install.** On a package-managed install
  — the Arch package, where the engine and data are root-owned system files — it now says so
  and points you at your package manager, instead of downloading 400 MB and failing with a
  permission error that explained nothing.

## 1.4.0.5397 — 2026-08-05
- **The game now paces itself to your monitor instead of running flat out.** A new install drew frames as
  fast as the processor allowed — frames the display never showed, with one core pinned at 100%. Worse, it
  left nothing in reserve: the game runs on a single thread, so a frame that needed more work than usual
  overran and was dropped. That is the intermittent stutter with no obvious cause. Drawing is now capped at
  the refresh rate your display reports, and when the machine cannot hold that rate the cap steps down to a
  whole fraction of it (144 → 72 → 48), which turns a dropped frame into one that still arrives on time. It
  settles rather than hunting, and steps back up once the headroom returns. A rate you set yourself in the
  configuration is an instruction and is never overridden. Multiplayer and replays are unaffected — the game
  advances at its own pace regardless of how often the screen is drawn.
- **The map view no longer costs a whole processor core.** With the map open, 46% of all CPU time went on
  stretching the parchment background to fill the screen — five million pixels at 3440×1440, every frame, on
  the one thread the game runs on — only to discard the result and do it again. That image is static; only
  what is drawn on top of it moves. It is now scaled once and kept, and rebuilt only when something it
  depends on genuinely changes. The wider your display, the more you get back.
- **Files that came along for the ride no longer appear as campaigns, mods and levels.** Archives built on
  macOS or Windows carry a small hidden companion file beside every real one, and campaigns and mods reach a
  Linux install as archives. The game was treating those companions as content: reading them as campaigns,
  handing them to the graphics loader, counting them as levels, and telling you on-screen to install one as
  a mod. They also sort ahead of the file they shadow, so anything choosing by position chose the wrong one.
  Hidden files are now ignored everywhere the game looks for content.

## 1.4.0.5384 — 2026-08-03
- **Multiplayer map packs work.** Classic, Modern and Original now appear in the multiplayer menu with
  their maps, in every install method. They are declared by text files this repository tracks, but the
  maps themselves live in the game data — and the update archive is assembled from the *previous*
  release's archive, so a directory that did not exist when that chain began could never join it. The
  mappacks arrived upstream a month after the 1.3.2 data this fork first packaged, which is exactly the
  gap they fell into. The archive is now layered over the KeeperFX team's current data package on every
  build, so their new content arrives on its own instead of waiting to be noticed.
- **The multiplayer service list reads correctly again.** It offered "Serial", "Modem" and "IPX" — the
  1997 game's options — where the engine actually builds "Online", "LAN" and "Skirmish". The names come
  from a string table that had drifted out of step with the engine reading it, for the same reason as
  above. Same fix, and it keeps the rest of the interface's text current too.
- **Campaigns and levels are current again.** Their configuration had been frozen at whatever the archive
  first shipped: 14 of 60 campaign files and 22 of 60 level files were behind the engine, including
  changes the team made in July. Nothing you need to do — an update simply brings them in step.
- **Install from the AUR on Arch.** `yay -S keeperfx-tux` builds the engine against your own system's
  libraries and pulls the game data in with it, so one command is the whole install and `yay -Syu` keeps
  it current. The AppImage stays for everything else, and the Flatpak still updates monthly.
- **The game window has an icon and a name.** It showed a generic placeholder in taskbars and docks under
  both X11 and Wayland; the launcher window did too.

## 1.4.0.5366 — 2026-08-03
- **`KFX_NONINTERACTIVE=1` for scripts and CI.** A startup problem — an unrecognised command-line option,
  say — raised a message box and waited for a click. Fine for a person, fatal for anything automated: CI,
  scripts, and the launcher's *extra launch options* field all hung until something timed out, with the
  reason visible only in the log. Set this variable and the engine logs the message and carries on. Unset,
  nothing changes, so ordinary players still get their dialogs.
- **Our own `music/MusicReadme.txt` now actually reaches you.** Shipping it in 5363 wasn't enough: the
  update archive is assembled from the *previous* release's payload, and its config overlay copied
  `fxdata`, `creatrs` and `mods` but not `music` — so a file only the engine repo knew about would have
  stayed frozen indefinitely. Caught by checking the readme's size inside the built archive before
  publishing. The same freezing bit this fork once before, which is why that overlay exists at all.

## 1.4.0.5363 — 2026-08-03
- **Your own music, any names, any format.** The game used to demand exactly `keeper02.ogg` …
  `keeper07.ogg` and play nothing otherwise. Now it uses whatever audio is in `music/`: name the files
  what you like (`Track 02.flac`, `02.wav`, or the original names), in **OGG, FLAC, WAV or MP3**. Files
  carrying a track number are placed by that number; files without one are used in alphabetical order.
  Nothing about an existing install changes — `keeperNN` names are still looked up first and still win,
  so the same files keep playing.
- **Silent music is no longer a mystery.** `keeperfx.log` now says how many tracks were found and names
  every file it did **not** use, with the reason — no track number, a better-quality copy of the same
  track winning, or more files present than there are tracks. Previously this was logged at a debug level
  no normal build emits, which is how a music folder holding nothing but its readme went unnoticed for
  weeks.
- **The launcher's "music missing" prompt actually reaches you.** It was only ever offered while copying
  the original DK files, so an otherwise-complete install could never be told its music was missing.
  It now checks at startup (with a "Don't show this again" box), and there's a **Download music** entry
  in the menu beside the Play button. The check also understands FLAC/WAV/MP3 soundtracks and symlinked
  music libraries, so a curated folder is never wrongly called empty — and a partly-failed copy now
  offers the download instead of quietly leaving you short a track.
- **Fixed: an OpenAL error at every startup.** `SetSoundMasterVolume: OpenAL error … Invalid operation`
  appeared in every log. The game window exists before the audio system starts, so the focus event fired
  on it reached the mixer too early. Cosmetic — audio was never affected — but the log is clean now.
- **`make` builds Linux.** A bare `make` ran the upstream Windows/mingw target, which also overwrote a
  shared dependency and left the next Linux build failing with a bare `cannot find -ljson`. It now builds
  Linux, and repairs that dependency automatically if a Windows build clobbered it.
- **A `music/` readme that is actually true.** The one shipped upstream still said "OGG format" only.
  Ours explains where to get the music, that filenames are free-form, which format to pick and why
  (OGG by default; FLAC only if you rip the CD yourself; MP3 avoided because its encoder padding makes an
  audible gap every time a track loops), and how to read the log when something isn't playing.
- **Upstream sync — 6 patches** (#5064–#5082): default campaign loaded when none is active (#5082), three
  new classic multiplayer maps (#5081), frameskip doubling (#5076), multiplayer lobby chat fixed (#5079),
  ctrl drag-click camera movement fixed (#5078), and a large `main.cpp` cleanup (#5064). All clean-merged.

## 1.4.0.5335 — 2026-08-01
- **Named: "KeeperFX — Tux Edition."** The native Linux build now has an identity — the game's main-menu
  stamp, the launcher window title, and the GNOME menu entry all read **KeeperFX Tux Edition**. (The repo
  slug, asset filenames and version string are intentionally unchanged for update-pipeline stability.)
- **Cleaner in-game menu.** The menu version stamp drops the volatile build number (it climbed every build);
  the exact version still lives in the launcher, `keeperfx.log`, and crash reports.
- **Cleaner README.** The three heaviest sections (full feature breakdown, upstream-sync explainer, DK1
  install guide) are now collapsible so newcomers aren't overwhelmed.

## 1.4.0.5332 — 2026-08-01
- **Weekly upstream sync (run early, by hand).** Pulled the KeeperFX team's latest **7 patches**
  (#5066–#5075) — a rendering-heavy batch that clean-merged with our fork:
  - **High-res / "straight view" rendering fixes** — a great batch for our ultrawide + 4K Linux setups:
    sprites no longer vanish at max zoom in 4K (#5066), units no longer show through walls (#5068), and
    door keys are no longer invisible (#5073), all in straight view; plus a rewrite of the polygon
    rasteriser `bflib_render_gpoly.c` (#5071).
  - **Smoother camera** — local camera controls are processed before the logic update (#5067) and
    catch-up packets handled after it (#5075).
  - **GUI** — message-button drop-animation fix (#5069).
- Also carries the launcher **Workshop instant-load caching** (thumbnails + catalogue cached to disk,
  refreshed in the background) that had been staged since 5322.

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
