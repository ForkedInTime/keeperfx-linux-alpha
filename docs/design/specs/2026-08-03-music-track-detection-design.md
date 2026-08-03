# Music track detection — replacing the hardcoded `keeperNN.ogg` requirement

**Date:** 2026-08-03
**Status:** Approved, ready for implementation
**Repos:** `ForkedInTime/keeperfx-linux-alpha` (engine), `ForkedInTime/keeperfx-launcher-qt-linux` (launcher)

## Problem

`play_music_track()` in `src/bflib_sndlib.cpp` resolves numbered tracks through a single hardcoded format string:

```c
return play_music(prepare_file_fmtpath(FGrp_Music, "keeper%02d.ogg", track));
```

A user who rips their own soundtrack, or extracts the CD to FLAC, must rename everything to `keeper02.ogg` … `keeper07.ogg` or get silence. The silence is near-undiagnosable: it produces only a `WARNLOG` line most players never see.

## Goal

Naming and format freedom: drop any set of audio files into `music/` and have them play, without renaming and without being restricted to OGG.

## What is already solved

`InitialiseSDLAudio()` (`src/bflib_sndlib.cpp`) already initialises the extra decoders:

```c
int flags = Mix_Init(MIX_INIT_OGG|MIX_INIT_MP3|MIX_INIT_FLAC);
```

WAV is built into SDL_mixer, and `play_music()` loads through `Mix_LoadMUS`, which detects format from file content rather than extension. **FLAC, WAV and MP3 therefore already play today** — nothing ever asks for those filenames. The entire remaining problem is filename resolution.

Directory enumeration is also already available and cross-platform: `LbFileFindFirst` / `LbFileFindNext` / `LbFileFindEnd` in `src/bflib_fileio.h`, used in `config_campaigns.c`, `custom_sprites.c` and `lvl_filesdk1.c`.

## Relationship to upstream PR #5061

Upstream has an open PR (#5061, since 2026-07-26) covering the same ground: a name-sorted listing of `music/` where sorted position maps to track number, plus FLAC/WAV/MP3 with a folder-wide format priority of FLAC > WAV > OGG > MP3.

We are not adopting it. Two concrete reasons:

1. **It is Windows-first.** It hand-rolls directory listing in `src/windows.cpp`; we reuse the existing cross-platform helper and need no new platform code.
2. **Sorted position is surprising.** Maintainer Loobinex could not get the PR to work in testing: he added one `adksong.mp3`, nothing changed, because the pre-existing `.ogg` set won the folder-wide format priority. Under that design, adding a single file can silently renumber an entire soundtrack.

Per the standing fork rule, our implementation supersedes upstream's. If #5061 merges, the weekly sync keeps ours. Watcher routine `trig_011J7DByk8xUQrZdcLPyJVvB` emails on merge or close, for awareness during syncs only.

## Design

### Engine: a cached music index

A file-static index in `src/bflib_sndlib.cpp` mapping track number → resolved absolute path, built lazily on first use and cached for the process lifetime.

1. Enumerate `music/` (`FGrp_Music`) with `LbFileFindFirst`/`LbFileFindNext`/`LbFileFindEnd`.
2. Ignore any entry whose filename starts with `.` before anything else is checked. This is not a Windows-porting nicety: `LbFileFindFirst`'s Linux implementation (`src/linux.cpp`) enumerates with raw `readdir` and matches with `fnmatch` but never passes `FNM_PERIOD`, so hidden entries come back from the enumeration. Without this filter, a macOS AppleDouble sidecar (`._keeper02.ogg`) would beat the real `keeper02.ogg` in every collision, because `.` (0x2E) sorts before ordinary letters — silently taking over the track if the sidecar fails to decode, with the real file sitting unused in the same folder. This is routine fallout from extracting a Mac-created zip or copying music via macOS onto exFAT, not a theoretical edge case.
3. Keep files whose extension is `.ogg`, `.flac`, `.wav` or `.mp3`, compared **case-insensitively** — this matters on Linux, where `KEEPER02.OGG` is a distinct name.
4. Build the track mapping (below).
5. `play_music_track()` tries the direct stock-named lookup first (see "Direct stock lookup is authoritative" below); only when that fails does it fall back to a lookup into the index. On either hit it calls the existing `play_music()` unchanged.

Everything lives in one self-contained helper so that a future upstream conflict in this function is small and reviewable.

### Direct stock lookup is authoritative

`play_music_track()` used to resolve every track through one hardcoded call: `prepare_file_fmtpath(FGrp_Music, "keeper%02d.ogg", track)`. That worked for **any** track number `game.music_track` could hold — including a campaign-defined `SET_MUSIC(8)` playing a `keeper08.ogg` the campaign shipped — because `set_music_check()` (`src/lvl_script_commands.c`) stores straight into `value->chars[0] = atoi(...)` with no range check, and `lua_PlayMusic()` / `console_cmd.c`'s music command accept the same unrestricted range.

The index above only ever discovers tracks the enumeration + mapping rule assigns — `MUSIC_TRACK_MIN`–`MUSIC_TRACK_MAX` (2–7) in numeric mode, or whatever numbering the sorted fallback happens to produce. Making the index the *only* resolution path — as an earlier version of this feature did — silently discarded any resolution the engine had for track numbers the index happened not to produce: `keeper08.ogg` + `SET_MUSIC(8)` worked before this feature existed and stopped working once the index became the sole lookup. That was the regression this direct lookup exists to undo.

The fix keeps this feature strictly additive. In the `Ft_NoCdMusic` branch, `play_music_track(track)` now:

1. **First**, tries a stock-named `keeper%02d` file for the requested track, trying each recognised extension in `MUSIC_DIRECT_LOOKUP_EXTENSIONS`' order — **OGG first** (`.ogg`, `.flac`, `.wav`, `.mp3`) — checking existence case-insensitively via `LbFileExists` (the same `find_case_insensitive_file` mechanism `play_music()` itself uses through `LbFileCaseInsensitivePath`). A hit `LbFileExists` reports that `play_music()` cannot actually load — `LbFileExists` is a bare `access(F_OK)`, which is also true for a directory named e.g. `keeper02.flac/` or a mode-000 file — falls through to the remaining extensions, and from there to the index, rather than dead-ending in silence while a playable index entry for that track sits unused.

   `MUSIC_DIRECT_LOOKUP_EXTENSIONS` is deliberately **not** the same order as the index's `MUSIC_EXTENSIONS` (FLAC first). The direct lookup existed before this feature and always found the OGG a stock install shipped, so an install with both a `keeper02.ogg` and a `keeper02.flac` side by side must keep playing the OGG — exactly what it played before this feature existed. The index's FLAC-first order is only a *preference*: the index has no pre-existing behaviour to preserve, so it is free to prefer lossless. Do not merge the two arrays.

   This is the engine's original, unrestricted lookup, restored — it resolves any track number, not just 2–7, and never sees a differently-named or malformed variant (e.g. `keeper02(1).ogg`) since it only ever probes the exact stock name.
2. **Otherwise**, consults the music index exactly as before.
3. **Otherwise**, emits the existing warn-once message and returns `false`.

This ordering means a stock install (or any install that happens to still use `keeperNN.*` naming) is resolved identically to before this feature existed, for every track — the index is consulted only for tracks/names the direct lookup cannot already satisfy. The governing rule: a convenience feature may only make *additional* tracks resolvable; it must never remove or change a resolution the engine already had.

`build_music_index()` is a pure function with no knowledge of this direct lookup, so the notes it produces (see "Mapping rule" below) necessarily describe only its own mapping decisions, not what actually plays. A file the index "drops" may still be exactly what plays, via the direct lookup above; a file the index keeps may be shadowed by it. Nothing in this design produces a combined "what actually played" account — a caller that wants one has to account for the direct lookup itself.

### Mapping rule

Extract the **last run of digits** from each basename as that file's candidate track number, with every trailing recognised extension removed first — repeatedly, not just once. A name has only one *real* extension, but a file like `keeper02.mp3.ogg` (re-exported without renaming, so its old extension survives as part of the name) has a second recognised-looking one immediately before the real one; stripping only the final `.ogg` would leave `keeper02.mp3`, whose trailing digit run is the `3` from `.mp3` — track 3, wrong. Stripping every trailing recognised extension in turn leaves `keeper02`, giving track 2, correctly. `keeper02.mp3.ogg` resolving as track 2 is exercised directly by `tests/test_music_index.cpp`.

`build_music_index()` builds **two** candidate mappings from the enumerated files and uses whichever resolves more tracks:

- **Numeric candidate:** every file whose candidate number falls in **2–7** claims that track (collisions resolved by the format-preference rule below); a file with no number, or a number outside 2–7, takes no part in the numeric candidate and is simply excluded from it.
- **Sorted candidate:** deduplicate the files by filename stem (see "Sorted-fallback deduplication" below), then assign the survivors to tracks 2, 3, 4, … in filename order; anything past track 7 is excluded.

**Whichever candidate resolves strictly more tracks wins; an equal count favours the numeric candidate**, since a numeric mapping reflects filenames the user (or a campaign) chose deliberately, while sorted position is incidental.

This replaces an earlier, all-or-nothing draft of the rule ("numeric mode only if every numbered file is in range, otherwise sorted fallback for everything"), which let a single out-of-range-numbered stray file force the *whole* folder into sorted mode — renumbering it and potentially truncating a working, otherwise-correctly-numbered set off the end (`Track 02.flac`…`Track 07.flac` plus a stray `bonus01.mp3` used to shift all six and lose `Track 07`). Comparing resolved counts instead means a stray file can never make the outcome worse than leaving it out would have: it can tie, but a complete numbered set is never beaten by adding one more file.

In sorted-fallback mode, files are assigned tracks from 2 upward; anything past track 7 is ignored and noted at debug level. In numeric mode, if two files in the **same** format claim the same track (`keeper03.ogg` and `track03.ogg`), the one earlier in sort order wins and the other is ignored, noted at debug level — the collision rule below only decides between *different* formats. A file excluded from the numeric candidate (no number, an out-of-range number, or too-large a number to be plausible) is also noted at debug level, worded to name the remedy rather than vanishing with no trace anywhere, which was the original form of this bug: a recognised audio file in `music/` that silently never played, with nothing in the log to explain why. Sorting, in both the sorted candidate and the tie-break, is by filename, ascending, case-insensitive.

Every row below was produced by running the live `build_music_index()` (`src/music_index.h`) — via `tests/run.sh` and a standalone probe compiled against the header directly — not asserted from memory:

| Files present | Numeric resolves | Sorted resolves | Outcome |
|---|---|---|---|
| `keeper02.ogg` … `keeper07.ogg` | 6 | 6 (tie) | numeric wins the tie — **identical to current behaviour** |
| `Track 02.flac` … `Track 07.flac` | 6 | 6 (tie) | numeric wins the tie |
| `music00.ogg` … `music05.ogg` | 4 (`music02`–`music05`; `00`/`01` out of range, excluded) | 6 | sorted wins → tracks 2–7 |
| `audiocd01.mp3` … `audiocd06.mp3` | 5 (`02`–`06`; `01` out of range, excluded) | 6 | sorted wins → tracks 2–7 |
| `dungeon.flac`, `battle.flac` | 0 (no file numbered) | 2 | sorted wins → tracks 2, 3 |
| `keeper02.ogg`, `bonus.flac` | 1 (`bonus.flac` has no number) | 2 | sorted wins → track 2 = `bonus.flac`, track 3 = `keeper02.ogg` |
| `keeper02.ogg` … `keeper07.ogg` plus `bonus.mp3` | 6 (`bonus.mp3` excluded, rest unaffected) | 6 (7 distinct stems, capped at 6) | numeric wins the tie → tracks 2–7 unchanged, `bonus.mp3` dropped with a note |
| `keeper02.ogg` … `keeper07.ogg` plus `bonus01.mp3` | 6 (`bonus01.mp3`'s number, 1, is out of range, excluded) | 6 (capped) | numeric wins the tie → tracks 2–7 unchanged |
| `Track 02.flac` … `Track 07.flac` plus a browser-style duplicate `Track 02 (1).flac` | 6 (the `(1)` parses as track 1, out of range, excluded) | 6 (capped) | numeric wins the tie → `Track 02.flac` keeps track 2, the duplicate dropped with a note |
| `keeper02.ogg` alone | 1 | 1 (tie) | numeric wins the tie |
| `track03.ogg`, `keeper03.ogg` (no other files) | 1 (both claim track 3; the collision rule keeps one, the other excluded) | 2 (distinct stems — no collision in sorted mode) | sorted wins → track 2 = `keeper03.ogg`, track 3 = `track03.ogg` |
| Six untitled files, one with an incidental digit (`Ambience.ogg`, `Battle Theme.ogg`, `Dungeon Keeper 2.ogg`, `Menu.ogg`, `Victory.ogg`, `War Drums.ogg`) | 1 (only `Dungeon Keeper 2.ogg` carries a number) | 6 | sorted wins → all six resolve, in filename order |
| `keeper02.ogg`, `keeper03.ogg`, `keeper03.flac`, `keeper04.ogg` … `keeper07.ogg` | 6 (FLAC wins track 3) | 6 (tie — `keeper03.ogg`/`keeper03.flac` dedup to one stem) | numeric wins the tie → FLAC wins track 3, other tracks unaffected |

The first and last rows are the regression guards: a stock install, and a same-track format collision, both resolve exactly as they did before this rule existed. The `bonus.mp3` / `bonus01.mp3` / `Track 02 (1).flac` rows are the regression guard for *this* rule: a stray file — unnumbered, out-of-range-numbered, or an accidental near-duplicate — can tie a complete numbered set but never beat it, so it is excluded rather than renumbering or truncating one.

The `keeper02.ogg` + `bonus.flac` row is a genuine, deliberate behaviour change from the all-or-nothing draft: both files are playable, so resolving both under sorted mode is strictly better than numeric mode's single track, and the tie-break correctly prefers it.

**Accepted edge case:** the bare `track03.ogg` + `keeper03.ogg` row is a known, marginal consequence of the same tie-break: with nothing else in the folder, stem dedup (which has no notion of track numbers) lets sorted mode resolve both files as two different songs, so it wins the tie over numeric mode's single-track collision resolution. In practice this is masked at runtime by the direct stock lookup (see "Direct stock lookup is authoritative" above), which resolves `keeper03.ogg` before the index is ever consulted.

Track 2 is the land view; 3–6 are cycled in-game by `player_utils.c`'s music-track cycling logic (`3 + (lvnum-1) % 4`); 7 is used by campaigns. Hence the 2–7 range.

### Sorted-fallback deduplication

Sorted fallback assigns tracks purely by position, so on its own it can be fooled by a library that keeps two formats of the same song side by side — a FLAC re-rip left next to the original OGGs, say. Sorted mode never compares by track number, so the collision rule below never gets a chance to apply to it; without dedup the two copies of one song would land on two different tracks, doubling that song up and, because sorted mode fills every slot it can, silently pushing a genuinely distinct song out of the 2–7 range entirely. A `01`/`02`/…-style prefix is exactly what forces sorted mode in the first place (see the `music00.ogg`/`audiocd01.mp3` rows above), so a user who transcodes such a library to FLAC and leaves the originals in place hits this every time — and because sorted mode still fills all six tracks, no "file dropped" warning fires; the symptom is only that the music is silently wrong.

Sorted fallback therefore deduplicates by filename **stem** (the basename with every trailing recognised extension removed — repeatedly, the same `music_strip_extensions()` used for the trailing-number extraction above — compared case-insensitively) before assigning positions: within each stem group, keep the best format by the same FLAC > WAV > OGG > MP3 preference described in the collision rule below — an equal-rank tie (same extension, different case) keeps whichever sorts first, exactly as the numeric-mode same-format tiebreak. The surviving representatives are then sorted by stem (case-insensitive, full filename as tiebreak) rather than by full filename, so the resulting track order stays stable no matter which format happened to win each group, and tracks are assigned from 2 upward as before. Every file dropped as a losing duplicate is noted, worded so it is clear a higher-preference copy of the same song won it, not that the file was rejected outright.

Stripping every trailing recognised extension (rather than just one) also means a double-extension pair such as `a.ogg` and `a.ogg.flac` — a file re-exported to FLAC without renaming, so its old `.ogg` survives as part of the name — dedup to the same stem (`a`) and collapse to the FLAC copy, exactly like an ordinary `a.ogg`/`a.flac` pair would. Verified directly by `tests/test_music_index.cpp`.

| Files present | Outcome |
|---|---|
| `01 intro.flac`/`.ogg`, `02 dungeon.flac`/`.ogg`, `03 battle.flac`/`.ogg`, `04 boss.flac`/`.ogg` | none of these stems end in a digit, so the numeric candidate resolves 0 tracks and sorted wins trivially; stem dedup collapses each pair to its FLAC copy → tracks 2–5 are the four FLAC files in song order, the four losing OGGs are noted and dropped, nothing is unreachable |
| `a.ogg`, `a.ogg.flac` | same stem (`a`) once both trailing extensions are stripped from the second file → dedup collapses to the FLAC copy, the OGG is dropped with a note |

This dedup applies only within sorted-fallback mode. Numeric mode's per-track collision rule below is unchanged: it already keys on track number, so two files sharing a number are already "the same track" and nothing about that rule needed to change.

### Collision rule

When two files claim the same track number (`keeper03.ogg` and `keeper03.flac`), prefer in order: **FLAC > WAV > OGG > MP3** — lossless first, then the better lossy codec.

This is applied **per track**, not folder-wide as in #5061. Mixed formats therefore coexist, and adding one file cannot renumber the others.

### Format guidance

All four recognised formats already decode natively, so no transcoding step is needed or wanted for any of them: `InitialiseSDLAudio()` calls `Mix_Init(MIX_INIT_OGG|MIX_INIT_MP3|MIX_INIT_FLAC)`, WAV is built into SDL_mixer, and `Mix_LoadMUS` detects format from file content rather than extension.

Recommended default: **OGG**. The source most people have is the original CD soundtrack as distributed in the keeperfx.net workshop pack, which is already lossy. A FLAC transcoded from that source is lossless-of-a-lossy: several times the size of the equivalent OGG, with no quality gain over it.

**FLAC** is worth the size only when ripping the original CD yourself — not when transcoding from an existing lossy rip. Expect roughly 4–5× the size of the equivalent OGG.

**WAV** has no case over FLAC: identical quality, much larger, no upside.

**Avoid MP3 for looping tracks.** Music loops via `Mix_PlayMusic(music, -1)`; MP3's encoder delay and padding produce an audible gap at every loop point that OGG, FLAC and WAV do not have (they are gapless).

**`MusicReadme.txt` — resolved in 1.4.0.5363.** Upstream's copy says "This directory will store music in OGG format", which predates format freedom and points a player with silent music away from the answer. It ships from the upstream distribution and is in neither repo, so the fork now carries its own at `config/music/MusicReadme.txt`, staged by `build-linux-alpha.yml` beside the fxdata/creatrs/mods the fork already overlays.

That alone was not enough to reach anyone. `full.7z` — the archive the self-updater downloads — is assembled by the launcher's `build-appimage.yml`, which overlays `fxdata`, `creatrs` and `mods` from the freshly-built engine but did not overlay `music`. Because each payload is built from the *previous release's* payload, a file only the engine repo knew about would have stayed frozen indefinitely: not this release, not any later one. Caught by checking the readme's byte size inside the built `full.7z` before publishing (537 bytes = upstream's, 2683 = ours). `music` was added to that overlay list. This is the second time this fork has shipped frozen payload config; the first prompted the overlay to exist at all.

### Launcher

`DkFiles::areAllSoundFilesPresent()` currently requires six exact `.ogg` filenames. Once the engine accepts any set, a valid FLAC soundtrack would still trigger the "music missing" prompt added earlier today — the same defect inverted.

It becomes: **does `music/` contain at least one playable audio file** (`.ogg`/`.flac`/`.wav`/`.mp3`, case-insensitive)?

Symlinks count. Linking a shared music library into `music/` is a normal way to curate a soundtrack without duplicating files, and the check this replaces used `QFile::exists()`, which follows symlinks — excluding them would be a silent regression.

Deliberately under-nag rather than false-nag. A user who has curated their own soundtrack must never be told their music is missing; the cost is that a partial set (say two files) no longer prompts. That trade is correct because a curated folder is a deliberate act while an empty folder is the actual failure being guarded against.

`DkFiles::musicFiles` and the copy loop in `copyDkDirToDir()` are **unchanged**. They read from an original Dungeon Keeper installation, which always uses `keeperNN.ogg`; only the presence check needs to relax.

### Error handling

- Empty or missing `music/`, or a requested track with no file: one `WARNLOG` naming the folder and stating how many tracks the music index mapped there. That count describes the index's own mapping, not everything the direct stock lookup might separately resolve (see "Direct stock lookup is authoritative" above), so it is not a claim that no music at all is playable.
- Emitted **once per track number**, not per call, so a level that retries does not flood the log.

### Out of scope

`play_music_fgroup()` and the mod/campaign music paths (`find_music_file_for_mod_list`, the `SET_MUSIC` script command) already accept arbitrary filenames and are a separate mechanism. Untouched.

## Verification

Neither repo has a test framework, so verification is a manual matrix, each case run and reported:

1. Stock `keeper02–07.ogg` → unchanged behaviour (regression guard)
2. `keeper02–07.flac` → plays
3. `Track 02.mp3` … `Track 07.mp3` → plays
4. `music00–05.ogg` → sorted fallback maps to tracks 2–7
5. `keeper03.ogg` + `keeper03.flac` together → FLAC wins, other tracks unaffected
6. Empty `music/` → one clear warning, no crash
7. Uppercase `KEEPER02.OGG` → found
8. Launcher with a FLAC-only folder → no "music missing" prompt

The user's own install is the fixture for case 1; `/mnt/Storage/SteamLibrary/steamapps/common/Dungeon Keeper/` holds the source oggs, and `ffmpeg` can produce the FLAC/MP3 variants for cases 2, 3 and 5.

Any binary built for testing must not be left in `~/.local/share/keeperfx-alpha/` unless built with `BUILD_NUMBER`/`VER_SUFFIX`, or its `1.4.0.0` version string confuses the version-gated self-update.

## Risks

| Risk | Mitigation |
|---|---|
| #5061 merges and conflicts in `play_music_track()` | Change confined to one helper; fork rule says keep ours; watcher gives warning |
| Case-sensitivity bugs on Linux | Extension and sort comparisons explicitly case-insensitive; test case 7 |
| Index cached too aggressively (files added while running) | Accepted. Scan once per process; restarting the game is a reasonable requirement for changing your soundtrack |
