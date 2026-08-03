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

The index above only ever discovers tracks the enumeration + mapping rule assigns, which for a stock or near-stock install is 2–7 (`MUSIC_STOCK_TRACK_MAX`) or whatever `MUSIC_TRACK_MAX` allows in numeric mode. Making the index the *only* resolution path — as an earlier version of this feature did — silently discarded any resolution the engine had for track numbers the index happened not to produce, which is exactly the regression described in "The regression" section this fix addresses: `keeper08.ogg` + `SET_MUSIC(8)` worked before this feature existed and stopped working once the index became the sole lookup.

The fix keeps this feature strictly additive. In the `Ft_NoCdMusic` branch, `play_music_track(track)` now:

1. **First**, tries a stock-named `keeper%02d` file for the requested track, trying each recognised extension in the same preference order the index uses (`MUSIC_EXTENSIONS`: flac, wav, ogg, mp3), checking existence case-insensitively via `LbFileExists` (the same `find_case_insensitive_file` mechanism `play_music()` itself uses through `LbFileCaseInsensitivePath`). This is the engine's original, unrestricted lookup, restored — it resolves any track number, not just 2–7, and never sees a differently-named or malformed variant (e.g. `keeper02(1).ogg`) since it only ever probes the exact stock name.
2. **Otherwise**, consults the music index exactly as before.
3. **Otherwise**, emits the existing warn-once message and returns `false`.

This ordering means a stock install (or any install that happens to still use `keeperNN.*` naming) is resolved identically to before this feature existed, for every track — the index is consulted only for tracks/names the direct lookup cannot already satisfy. The governing rule: a convenience feature may only make *additional* tracks resolvable; it must never remove or change a resolution the engine already had.

### Mapping rule

Extract the **last run of digits** from each basename as that file's candidate track number. Then a single decision:

> Use the numeric mapping if **at least one** file yields a number **and** every file that yields a number falls within the valid track range **2–7**. Files that yield no number take no part in the decision and are simply left out of the numeric mapping.
> Otherwise (no file yields a number at all, or some file's number falls outside 2–7) discard the numbers entirely and map sorted position → tracks 2, 3, 4, …

An unnumbered file dropped alongside a fully-numbered set (`keeper02.ogg` … `keeper07.ogg` plus a stray `bonus.mp3`) is therefore just ignored — the numbered set is unaffected. This is a deliberate change from an earlier draft of this rule, which required *every* file to yield a number: that version fell back to sorted order the moment a single unnumbered file appeared, which meant a correct, fully-numbered install could be silently renumbered (and lose its highest track) by dropping in one stray extra file. A file whose number is genuinely out of the 2–7 range is different — the filenames can no longer describe a complete numbered set, so that still forces the sorted-position fallback for everything. Sorting, in both the fallback and the tie-break below, is by filename, ascending, case-insensitive.

In sorted-fallback mode, files are assigned tracks from 2 upward; anything past track 7 is ignored and noted at debug level. In numeric mode, if two files in the **same** format claim the same track (`keeper03.ogg` and `track03.ogg`), the one earlier in sort order wins and the other is ignored, noted at debug level — the collision rule below only decides between *different* formats. An unnumbered file ignored by numeric mode (the `bonus.flac`/`bonus.mp3` rows below) is also noted at debug level, worded so it names the remedy — that the file has no track number in its name and can be given one — rather than vanishing with no trace anywhere, which was the original form of this bug: a recognised audio file in `music/` that silently never played, with nothing in the log to explain why.

| Files present | Extracted | Outcome |
|---|---|---|
| `keeper02.ogg` … `keeper07.ogg` | 2–7 | numeric — **identical to current behaviour** |
| `Track 02.flac` … `Track 07.flac` | 2–7 | numeric |
| `music00.ogg` … `music05.ogg` | 0–5 | 0 and 1 out of range → sorted → tracks 2–7 |
| `audiocd01.mp3` … `audiocd06.mp3` | 1–6 | 1 out of range → sorted → tracks 2–7 |
| `dungeon.flac`, `battle.flac` | none | no file numbered → sorted → tracks 2, 3 (4–7 warn once each) |
| `keeper02.ogg`, `bonus.flac` | 2, none | `bonus.flac` yields no number → ignored → numeric, track 2 = `keeper02.ogg` only |
| `keeper02.ogg` … `keeper07.ogg` plus `bonus.mp3` | 2–7, none | `bonus.mp3` yields no number → ignored → numeric, tracks 2–7 unchanged |

The first row is the regression guard: a stock install takes the numeric path and resolves to exactly the files it resolves to today. The second-to-last and last rows are the regression guard for *this* rule: an unnumbered stray file must never renumber or discard a working numbered soundtrack.

Track 2 is the land view; 3–6 are in-game (`player_utils.c:856` cycles `3 + (lvnum-1) % 4`); 7 is used by campaigns. Hence the 2–7 range.

### Sorted-fallback deduplication

Sorted fallback assigns tracks purely by position, so on its own it can be fooled by a library that keeps two formats of the same song side by side — a FLAC re-rip left next to the original OGGs, say. Sorted mode never compares by track number, so the collision rule below never gets a chance to apply to it; without dedup the two copies of one song would land on two different tracks, doubling that song up and, because sorted mode fills every slot it can, silently pushing a genuinely distinct song out of the 2–7 range entirely. A `01`/`02`/…-style prefix is exactly what forces sorted mode in the first place (see the `music00.ogg`/`audiocd01.mp3` rows above), so a user who transcodes such a library to FLAC and leaves the originals in place hits this every time — and because sorted mode still fills all six tracks, no "file dropped" warning fires; the symptom is only that the music is silently wrong.

Sorted fallback therefore deduplicates by filename **stem** (the basename with its extension removed, compared case-insensitively) before assigning positions: within each stem group, keep the best format by the same FLAC > WAV > OGG > MP3 preference described in the collision rule below — an equal-rank tie (same extension, different case) keeps whichever sorts first, exactly as the numeric-mode same-format tiebreak. The surviving representatives are then sorted by stem (case-insensitive, full filename as tiebreak) rather than by full filename, so the resulting track order stays stable no matter which format happened to win each group, and tracks are assigned from 2 upward as before. Every file dropped as a losing duplicate is noted, worded so it is clear a higher-preference copy of the same song won it, not that the file was rejected outright.

| Files present | Outcome |
|---|---|
| `01 intro.flac`/`.ogg`, `02 dungeon.flac`/`.ogg`, `03 battle.flac`/`.ogg`, `04 boss.flac`/`.ogg` | `01`-style prefix forces sorted mode; stem dedup collapses each pair to its FLAC copy → tracks 2–5 are the four FLAC files in song order, the four losing OGGs are noted and dropped, nothing is unreachable |

This dedup applies only within sorted-fallback mode. Numeric mode's per-track collision rule below is unchanged: it already keys on track number, so two files sharing a number are already "the same track" and nothing about that rule needed to change.

### Collision rule

When two files claim the same track number (`keeper03.ogg` and `keeper03.flac`), prefer in order: **FLAC > WAV > OGG > MP3** — lossless first, then the better lossy codec.

This is applied **per track**, not folder-wide as in #5061. Mixed formats therefore coexist, and adding one file cannot renumber the others.

### Launcher

`DkFiles::areAllSoundFilesPresent()` currently requires six exact `.ogg` filenames. Once the engine accepts any set, a valid FLAC soundtrack would still trigger the "music missing" prompt added earlier today — the same defect inverted.

It becomes: **does `music/` contain at least one playable audio file** (`.ogg`/`.flac`/`.wav`/`.mp3`, case-insensitive)?

Symlinks count. Linking a shared music library into `music/` is a normal way to curate a soundtrack without duplicating files, and the check this replaces used `QFile::exists()`, which follows symlinks — excluding them would be a silent regression.

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
