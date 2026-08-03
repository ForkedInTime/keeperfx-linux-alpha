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

`InitialiseSDLAudio()` (`src/bflib_sndlib.cpp:1147`) already initialises the extra decoders:

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
2. Keep files whose extension is `.ogg`, `.flac`, `.wav` or `.mp3`, compared **case-insensitively** — this matters on Linux, where `KEEPER02.OGG` is a distinct name.
3. Build the track mapping (below).
4. `play_music_track()` becomes a lookup into the index; on a hit it calls the existing `play_music()` unchanged.

Everything lives in one self-contained helper so that a future upstream conflict in this function is small and reviewable.

### Mapping rule

Extract the **last run of digits** from each basename as that file's candidate track number. Then a single decision:

> Use the numeric mapping if — and only if — **every** file yields a number **and** every one of those numbers falls within the valid track range **2–7**.
> Otherwise discard the numbers entirely and map sorted position → tracks 2, 3, 4, …

So a folder mixing `keeper02.ogg` with an unnumbered `bonus.flac` falls back to sorted order, because the numeric mapping cannot account for every file. Sorting is by filename, ascending, case-insensitive.

In sorted-fallback mode, files are assigned tracks from 2 upward; anything past track 7 is ignored and noted at debug level. In numeric mode, if two files in the **same** format claim the same track (`keeper03.ogg` and `track03.ogg`), the one earlier in sort order wins and the other is ignored, noted at debug level — the collision rule below only decides between *different* formats.

| Files present | Extracted | Outcome |
|---|---|---|
| `keeper02.ogg` … `keeper07.ogg` | 2–7 | numeric — **identical to current behaviour** |
| `Track 02.flac` … `Track 07.flac` | 2–7 | numeric |
| `music00.ogg` … `music05.ogg` | 0–5 | 0 and 1 out of range → sorted → tracks 2–7 |
| `audiocd01.mp3` … `audiocd06.mp3` | 1–6 | 1 out of range → sorted → tracks 2–7 |
| `dungeon.flac`, `battle.flac` | none | no numbers → sorted → tracks 2, 3 (4–7 warn once each) |
| `keeper02.ogg`, `bonus.flac` | 2, none | not every file numbered → sorted → tracks 2, 3 |

The first row is the regression guard: a stock install takes the numeric path and resolves to exactly the files it resolves to today.

Track 2 is the land view; 3–6 are in-game (`player_utils.c:856` cycles `3 + (lvnum-1) % 4`); 7 is used by campaigns. Hence the 2–7 range.

### Collision rule

When two files claim the same track number (`keeper03.ogg` and `keeper03.flac`), prefer in order: **FLAC > WAV > OGG > MP3** — lossless first, then the better lossy codec.

This is applied **per track**, not folder-wide as in #5061. Mixed formats therefore coexist, and adding one file cannot renumber the others.

### Launcher

`DkFiles::areAllSoundFilesPresent()` currently requires six exact `.ogg` filenames. Once the engine accepts any set, a valid FLAC soundtrack would still trigger the "music missing" prompt added earlier today — the same defect inverted.

It becomes: **does `music/` contain at least one playable audio file** (`.ogg`/`.flac`/`.wav`/`.mp3`, case-insensitive)?

Deliberately under-nag rather than false-nag. A user who has curated their own soundtrack must never be told their music is missing; the cost is that a partial set (say two files) no longer prompts. That trade is correct because a curated folder is a deliberate act while an empty folder is the actual failure being guarded against.

`DkFiles::musicFiles` and the copy loop in `copyDkDirToDir()` are **unchanged**. They read from an original Dungeon Keeper installation, which always uses `keeperNN.ogg`; only the presence check needs to relax.

### Error handling

- Empty or missing `music/`, or a requested track with no file: one `WARNLOG` naming the folder and stating how many playable files were found.
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
