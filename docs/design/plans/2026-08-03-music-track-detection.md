# Music Track Detection Implementation Plan

**Goal:** Let any set of audio files dropped into `music/` be picked up as the game's numbered tracks, replacing the hardcoded `keeper%02d.ogg` lookup.

**Architecture:** The track-mapping rule lives in a new header-only file, `src/music_index.h`, with no game, SDL or OpenAL dependency, so it can be unit-tested standalone. `src/bflib_sndlib.cpp` enumerates `music/` once via the existing cross-platform `LbFileFindFirst`/`LbFileFindNext`, feeds the filenames through that pure function, caches the result, and turns `play_music_track()` into a lookup. The launcher's "is music present" check relaxes to match.

**Tech Stack:** C++ (engine, tabs for indentation), C++/Qt 6 (launcher, 4 spaces), GNU make. No test framework exists in either repo; Task 1 introduces a standalone assert-based test binary for the pure logic, and Tasks 2–3 are verified by a manual matrix.

## Global Constraints

- Valid track numbers are **2–7**. 2 is the land view, 3–6 are cycled in-game by `player_utils.c`'s music-track cycling logic (`3 + (lvnum-1) % 4`), 7 is used by campaigns.
- Recognised extensions: `.ogg`, `.flac`, `.wav`, `.mp3` — compared **case-insensitively** (Linux filesystems are case-sensitive).
- Same-track format preference: **FLAC > WAV > OGG > MP3**, applied per track, never folder-wide.
- **Do not modify `linux.mk` or `Makefile`.** Both are upstream-owned; the header-only design exists specifically to avoid touching them.
- **Do not touch** `play_music_fgroup()`, `find_music_file_for_mod_list()`, or the `SET_MUSIC` script path. Those already accept arbitrary filenames and are a separate mechanism.
- Engine C++ files use **tab** indentation; launcher files use **4 spaces**. Match the file you are editing.
- A test build of the engine must **not** be left installed in `~/.local/share/keeperfx-alpha/` unless built with `BUILD_NUMBER`/`VER_SUFFIX`, or its `1.4.0.0` version string confuses the version-gated self-update.
- Build the engine with `make -f linux.mk -j"$(nproc)"` (or bare `make`, which the fork's `GNUmakefile` forwards). Build the launcher with `cmake --build /mnt/Storage/Projects/keeperfx-workspace/launcher/build -j"$(nproc)"`.

---

## File Structure

| File | Repo | Responsibility |
|---|---|---|
| `src/music_index.h` | engine | **Create.** Header-only pure logic: extension test, number extraction, track mapping. No I/O. |
| `tests/test_music_index.cpp` | engine | **Create.** Standalone assert-based tests for the above. |
| `src/bflib_sndlib.cpp` | engine | **Modify.** Enumerate `music/`, cache the index, rewrite `play_music_track()`'s disk branch. |
| `src/dkfiles.h` / `src/dkfiles.cpp` | launcher | **Modify.** Replace `areAllSoundFilesPresent()` with `isAnyMusicPresent()`. |
| `src/copydkfilesdialog.cpp`, `src/launchermainwindow.cpp` | launcher | **Modify.** Update the two call sites. |

---

### Task 1: Pure track-mapping logic

**Files:**
- Create: `src/music_index.h`
- Test: `tests/test_music_index.cpp`

**Interfaces:**
- Consumes: nothing (standard library only).
- Produces:
  - `std::map<int, std::string> build_music_index(const std::vector<std::string> & entries, std::vector<std::string> * notes = nullptr)` — takes bare filenames from a directory listing, returns track number → filename. When `notes` is non-null, appends one human-readable line for every recognised audio file dropped from the result (format-collision loser, sorted-mode overflow past track 7, or — in numeric mode — a file with no track number in its name), so a caller can tell the user why a file sitting in `music/` is not playing.
  - `int music_extension_rank(const std::string & fname)` — index into the preference list, `-1` if not audio.
  - `int music_trailing_number(const std::string & fname)` — last digit run in the stem, `-1` if none.
  - Constants `MUSIC_TRACK_MIN` (2) and `MUSIC_TRACK_MAX` (7).

- [ ] **Step 1: Write the failing test**

The implementation lives in `tests/test_music_index.cpp` — read the file; it is the source of truth. It is deliberately not reproduced here, because embedded copies on this plan drifted five times.

What it must cover: standalone, assert-based tests of `build_music_index()`, `music_extension_rank()` and `music_trailing_number()` from `src/music_index.h`, buildable and runnable with no engine/game dependency (`g++ -std=c++17 -Wall -Wextra -o bin/test_music_index tests/test_music_index.cpp && ./bin/test_music_index`, or simply `tests/run.sh`). At minimum:

- A stock all-numbered set (`keeper02.ogg` … `keeper07.ogg`) resolving identically to the old hardcoded path — the core regression guard.
- Alternate-format and alternate-naming numbered sets (FLAC, spaces in names).
- Zero-based and one-based numbering forcing the sorted fallback.
- A stray unnumbered or out-of-range-numbered file alongside a complete numbered set, confirming it cannot renumber or truncate the set — the regression guard for upstream #5061's renumbering bug.
- The numeric/sorted tiebreak added post-review (build both candidates, strictly-more-tracks wins, ties favour numeric) — see the design spec's "Mapping rule" section for the current worked-example table and rationale, including the deliberate behaviour change where a lone numbered file plus one unnumbered file now resolves via sorted mode instead of numeric.
- The FLAC > WAV > OGG > MP3 collision rule, both across formats and within the same format (sort-order tiebreak).
- Case-insensitive names and extensions.
- Non-audio entries, empty input, and dotfile/AppleDouble-sidecar entries (must not win a collision purely by sorting first).
- Overflow-safe trailing-number parsing (an absurdly large digit run must not wrap into a bogus small track).
- Sorted-fallback stem dedup, including the double-extension case (`a.ogg` vs `a.ogg.flac` deduping to the same song).
- The `notes` output naming why each dropped file was excluded, worded to describe the index's own mapping decision rather than actual playback — the direct stock lookup is a separate mechanism this pure function has no visibility into.

Do not hand-author a divergent version of these tests from an older draft of this plan — read the live file.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /mnt/Storage/Projects/keeperfx-alpha
mkdir -p bin
g++ -std=c++17 -Wall -Wextra -o bin/test_music_index tests/test_music_index.cpp
```

Expected: FAILS to compile with `fatal error: ../src/music_index.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

The implementation lives in `src/music_index.h` — read the file; it is the source of truth. It is deliberately not reproduced here, because embedded copies on this plan drifted five times.

What the file must provide, header-only with no game/SDL/OpenAL dependency (so `tests/test_music_index.cpp` can exercise it standalone, and so neither `linux.mk` nor `Makefile` — both upstream-owned — needs a new source-file entry):

- `MUSIC_TRACK_MIN` (2), `MUSIC_TRACK_MAX` (7).
- `MUSIC_EXTENSIONS` — FLAC, WAV, OGG, MP3, in the index's own per-track collision preference order — and `music_extension_rank()`.
- `music_trailing_number()` — the last run of digits in a basename, with every trailing recognised extension stripped repeatedly (not just once: a name like `keeper02.mp3.ogg` has a second recognised-looking extension embedded right before the real one, and stripping only the outermost one would misread the `3` in `.mp3` as the track number). Overflow-safe: a huge digit run must not silently wrap into a small bogus track number.
- `build_music_index()` — takes bare filenames, returns track number → filename. It builds two candidate mappings and returns whichever resolves the map:
  - a **numeric** candidate: every file with a trailing number in 2–7 claims that track, format collisions resolved by the preference order above;
  - a **sorted** candidate: dedup the files by filename stem (stripped the same repeated way), then assign surviving files to tracks 2, 3, 4, … by filename order, capped at 7.

  Whichever candidate resolves **strictly more** tracks wins; an equal count favours numeric. This tiebreak is a post-review revision of the rule — the design spec's "Mapping rule" section has the full rationale, the current worked-example table (verified against the live code, not asserted from memory), and the reasoning for why ties favour numeric over an earlier all-or-nothing draft that let one stray file renumber or truncate a whole working soundtrack.
- An optional `notes` output parameter: one line per recognised audio file the function itself chose not to use, worded to name the remedy where one exists. These notes describe **only the index's own decision** — the function has no visibility into `play_music_track()`'s separate direct stock-name lookup (see the design spec), so a file it "drops" may still be exactly what plays via that other path, and a file it keeps may be shadowed by it.
- Dotfile/hidden-entry filtering before any of the above, so macOS AppleDouble sidecars (`._keeper02.ogg`) can never win a collision purely because `.` sorts first.

Do not hand-author a divergent copy of this logic from an older draft of this plan — read the live file.

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /mnt/Storage/Projects/keeperfx-alpha
g++ -std=c++17 -Wall -Wextra -o bin/test_music_index tests/test_music_index.cpp && ./bin/test_music_index
```

Expected: every line prefixed `ok`, final line `All music index tests passed.`, exit status 0. There must be no compiler warnings.

- [ ] **Step 5: Commit**

```bash
cd /mnt/Storage/Projects/keeperfx-alpha
git add src/music_index.h tests/test_music_index.cpp
git commit -m "feat(music): pure track-mapping logic for the music folder

Header-only so neither linux.mk nor Makefile needs an entry (both are
upstream-owned) and so the logic can be unit-tested without linking the
engine. Numeric mode requires every file to carry an in-range track number;
anything else falls back to sorted order, which avoids upstream #5061's
behaviour where adding one file silently renumbers the whole soundtrack."
```

---

### Task 2: Wire the index into the engine

**Files:**
- Modify: `src/bflib_sndlib.cpp` (the include sits right after the existing `bflib_fileio.h` include near the top of the file; the cache/enumerator globals sit next to the existing `g_current_music_track` global; `play_music_track()` is further down in the same file. Exact line numbers are not cited here because they drift with every unrelated change elsewhere in this large file — search for the symbol names instead.)

**Interfaces:**
- Consumes: `build_music_index()`, `MUSIC_TRACK_MIN`, `MUSIC_TRACK_MAX` from Task 1. `LbFileFindFirst`/`LbFileFindNext`/`LbFileFindEnd` and `struct TbFileEntry { const char * Filename; }` from `bflib_fileio.h` (already included near the top of the file, immediately before the new `music_index.h` include). `prepare_file_path_buf(char *dst, int dst_size, short fgroup, const char *fname)` and `prepare_file_fmtpath(short fgroup, const char *fmt_str, ...)`, both already used in this file.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Add the include**

In `src/bflib_sndlib.cpp`, immediately after the existing `#include "bflib_fileio.h"` line, add:

```cpp
#include "music_index.h"
```

- [ ] **Step 2: Add the cache and the enumerator**

In `src/bflib_sndlib.cpp`, immediately after the existing line `int g_current_music_track = 0;     // 0 if a custom file (or nothing) is playing`, add:

```cpp
// Cached mapping of track number -> filename in music/, built on first use.
// Rebuilding is not supported: changing your soundtrack requires a restart.
//
// Thread-safety: this cache is built lazily with no synchronisation. That is
// safe only because every play_music_track() call site runs on the main
// thread, and on_music_finished() -- the SDL_mixer callback that can run off
// the main thread -- never calls music_index() or touches this cache. If a
// future caller needs to build or read the index from another thread (async
// loading, preloading, etc.) this needs a lock.
std::map<int, std::string> g_music_index;
bool g_music_index_built = false;
std::vector<int> g_music_warned_tracks;
// Directory music_index() enumerated, kept only so the "no music file" warning
// below can name where it looked.
std::string g_music_dir;

const std::map<int, std::string> & music_index() {
	if (!g_music_index_built) {
		g_music_index_built = true;
		std::vector<std::string> entries;
		char spec[2048];
		prepare_file_path_buf(spec, sizeof(spec), FGrp_Music, "*");
		{
			// spec is ".../music/*"; strip the enumeration glob to get just
			// the directory for diagnostics.
			const std::string spec_str(spec);
			const std::string::size_type slash = spec_str.find_last_of('/');
			g_music_dir = (slash == std::string::npos) ? spec_str : spec_str.substr(0, slash);
		}
		struct TbFileEntry fe;
		struct TbFileFind * ff = LbFileFindFirst(spec, &fe);
		if (ff) {
			do {
				entries.push_back(fe.Filename);
			} while (LbFileFindNext(ff, &fe) >= 0);
			LbFileFindEnd(ff);
		}
		std::vector<std::string> notes;
		g_music_index = build_music_index(entries, &notes);
		SYNCDBG(7, "Music index built: %d playable track(s) from %d directory entr(ies)",
			(int)g_music_index.size(), (int)entries.size());
		for (std::size_t i = 0; i < notes.size(); ++i) {
			SYNCDBG(7, "%s", notes[i].c_str());
		}
	}
	return g_music_index;
}
```

- [ ] **Step 3: Rewrite the disk branch of `play_music_track()`**

The current implementation lives in `src/bflib_sndlib.cpp` — read `play_music_track()`; it is the source of truth. It is deliberately not reproduced here, because the embedded copy on this plan drifted twice.

What this step originally established: replace the old hardcoded disk branch (`play_music(prepare_file_fmtpath(FGrp_Music, "keeper%02d.ogg", track))`) with a lookup into `music_index()`, returning `false` with a warn-once `WARNLOG` when the requested track isn't in it, and otherwise calling the existing `play_music()` unchanged — which already skips restarting when the resolved file is already playing.

**This description is now historical.** Two fixes landed after this plan's initial execution and changed the function beyond what is described above:

- `1ebcc3294` / `846e6c6af` added a direct stock-named (`keeper%02d.*`) lookup that runs **before** the index is ever consulted, so a stock or near-stock install resolves exactly as it did before this feature existed for any track number, not just the index's 2–7.
- `ea97f4257` made that direct lookup probe `.ogg` first via its own `MUSIC_DIRECT_LOOKUP_EXTENSIONS` array (deliberately not the index's FLAC-first `MUSIC_EXTENSIONS` order), made a direct-lookup hit that cannot actually be played fall through to the remaining extensions and then to the index, and reworded the `WARNLOG` so its track count describes the index's own mapping rather than implying it is a count of everything playable.

The design spec's "Direct stock lookup is authoritative" and "Mapping rule" sections describe the function's current shape; treat this step as context for how the index first got wired in, not as a description of what ships today.

- [ ] **Step 4: Build**

```bash
cd /mnt/Storage/Projects/keeperfx-alpha
make -f linux.mk -j"$(nproc)" 2>&1 | grep -iE '\berror\b|warning.*sndlib' ; ls -la bin/keeperfx
```

Expected: no errors, `bin/keeperfx` newly timestamped.

- [ ] **Step 5: Verify case 1 — stock install is unchanged (regression guard)**

```bash
INSTALL=/home/yetipaw/.local/share/keeperfx-alpha
cp "$INSTALL/keeperfx" "$INSTALL/keeperfx.bak-musicidx"
cp /mnt/Storage/Projects/keeperfx-alpha/bin/keeperfx "$INSTALL/keeperfx"
ls "$INSTALL/music/"                     # expect keeper02..07.ogg + MusicReadme.txt
cd "$INSTALL" && timeout 30 ./keeperfx >/dev/null 2>&1
grep -iE 'No music file|Cannot load music' "$INSTALL/keeperfx.log" || echo "OK: no music warnings"
```

Expected: `OK: no music warnings`, and music audibly plays when a level is started.

- [ ] **Step 6: Verify cases 2–6 — alternate formats and layouts**

Work in a scratch copy so the real folder is never at risk:

```bash
INSTALL=/home/yetipaw/.local/share/keeperfx-alpha
SCRATCH=/tmp/keeperfx-music-test
mkdir -p "$SCRATCH/backup"
cp "$INSTALL"/music/*.ogg "$SCRATCH/backup/"

# Case 2 - FLAC set
rm -f "$INSTALL"/music/*.ogg
for n in 02 03 04 05 06 07; do
  ffmpeg -loglevel error -y -i "$SCRATCH/backup/keeper$n.ogg" "$INSTALL/music/keeper$n.flac"
done
cd "$INSTALL" && timeout 30 ./keeperfx >/dev/null 2>&1
grep -iE 'No music file|Cannot load music' "$INSTALL/keeperfx.log" || echo "case 2 OK"

# Case 4 - sorted fallback, zero-based names
rm -f "$INSTALL"/music/*.flac
i=0; for n in 02 03 04 05 06 07; do
  cp "$SCRATCH/backup/keeper$n.ogg" "$INSTALL/music/music0$i.ogg"; i=$((i+1))
done
cd "$INSTALL" && timeout 30 ./keeperfx >/dev/null 2>&1
grep -iE 'No music file' "$INSTALL/keeperfx.log" || echo "case 4 OK"

# Case 6 - empty folder warns exactly once per track, no crash
rm -f "$INSTALL"/music/*.ogg
cd "$INSTALL" && timeout 30 ./keeperfx >/dev/null 2>&1
grep -c 'No music file for track' "$INSTALL/keeperfx.log"

# Restore
rm -f "$INSTALL"/music/*.ogg "$INSTALL"/music/*.flac
cp "$SCRATCH/backup"/*.ogg "$INSTALL/music/"
ls "$INSTALL/music/"
```

Expected: `case 2 OK`, `case 4 OK`; the empty-folder count is small (one per distinct track reached, never growing with time); no crash in any case; the stock oggs are back at the end.

- [ ] **Step 7: Restore the official binary**

```bash
INSTALL=/home/yetipaw/.local/share/keeperfx-alpha
mv "$INSTALL/keeperfx.bak-musicidx" "$INSTALL/keeperfx"
ls -la "$INSTALL/keeperfx" "$INSTALL/music/"
```

Expected: the official binary is back and all six oggs are present. This matters because the locally built binary reports version `1.4.0.0`.

- [ ] **Step 8: Commit**

```bash
cd /mnt/Storage/Projects/keeperfx-alpha
git add src/bflib_sndlib.cpp
git commit -m "feat(music): resolve numbered tracks from whatever is in music/

play_music_track() no longer demands keeper%02d.ogg. The music folder is
enumerated once through the existing cross-platform LbFileFindFirst helpers,
mapped by src/music_index.h, and cached. A missing track now warns once
naming how many playable tracks were found, instead of failing quietly."
```

---

### Task 3: Relax the launcher's music-present check

**Files:**
- Modify: `/mnt/Storage/Projects/keeperfx-workspace/launcher/src/dkfiles.h` (the `areAllSoundFilesPresent()` declaration)
- Modify: `/mnt/Storage/Projects/keeperfx-workspace/launcher/src/dkfiles.cpp` (`areAllSoundFilesPresent()`)
- Modify: `/mnt/Storage/Projects/keeperfx-workspace/launcher/src/copydkfilesdialog.cpp` (the `areAllSoundFilesPresent()` call site)
- Modify: `/mnt/Storage/Projects/keeperfx-workspace/launcher/src/launchermainwindow.cpp` (the missing-music startup check)

**Interfaces:**
- Consumes: nothing from Tasks 1–2 — the launcher and engine share no code, only the `music/` convention.
- Produces: `static bool DkFiles::isAnyMusicPresent()` replacing `static bool DkFiles::areAllSoundFilesPresent()`.

- [ ] **Step 1: Rename the declaration**

In `src/dkfiles.h`, replace:

```cpp
    static bool areAllSoundFilesPresent();
```

with:

```cpp
    static bool isAnyMusicPresent();
```

`DkFiles::musicFiles` stays exactly as it is — it still describes what to copy from an original Dungeon Keeper installation, which always uses `keeperNN.ogg`.

- [ ] **Step 2: Reimplement it**

In `src/dkfiles.cpp`, replace the whole existing `areAllSoundFilesPresent()` function:

```cpp
bool DkFiles::areAllSoundFilesPresent()
{
    // Loop trough music files
    for (const QString& musicFileName : musicFiles) {
        // Get the destination file
        QString destFilePath = QCoreApplication::applicationDirPath() + "/music/" + musicFileName.toLower();
        QFile destFile(destFilePath);

        // Check if file exists
        if (destFile.exists() == false) {
            return false;
        }
    }

    return true;
}
```

with:

```cpp
bool DkFiles::isAnyMusicPresent()
{
    // The engine resolves track numbers from whatever audio files are in music/,
    // so an exact keeperNN.ogg list is no longer a valid test: a user with a FLAC
    // soundtrack has working music and must never be told it is missing.
    // Under-reporting is the deliberate trade -- a curated folder is an intentional
    // act, whereas an empty one is the failure actually worth prompting about.
    static const QStringList musicExtensions = {"ogg", "flac", "wav", "mp3"};

    QDir musicDir(QCoreApplication::applicationDirPath() + "/music");
    if (musicDir.exists() == false) {
        return false;
    }

    // Symlinks are followed deliberately: linking a shared music library into
    // music/ is a normal way to curate a soundtrack without duplicating files,
    // and the code this replaced used QFile::exists(), which follows them too.
    // This is the one function in this file that intentionally does NOT use
    // QDir::NoSymLinks -- dkfiles.cpp's isOriginalDkExecutableFound() and the
    // subdirectory scan both use that flag, but they are checking for an
    // actual Dungeon Keeper install, not a curated music folder, so a symlink
    // there would be suspicious rather than normal. Do not "fix" this for
    // consistency with those two; the inconsistency is deliberate.
    const QFileInfoList entries = musicDir.entryInfoList(QDir::Files);
    for (const QFileInfo& entry : entries) {
        if (musicExtensions.contains(entry.suffix().toLower())) {
            return true;
        }
    }

    return false;
}
```

- [ ] **Step 3: Update the two call sites**

In `src/copydkfilesdialog.cpp`, replace:

```cpp
    if (DkFiles::areAllSoundFilesPresent() == false) {
```

with:

```cpp
    if (DkFiles::isAnyMusicPresent() == false) {
```

In `src/launchermainwindow.cpp`, replace:

```cpp
        && DkFiles::areAllSoundFilesPresent() == false
```

with:

```cpp
        && DkFiles::isAnyMusicPresent() == false
```

- [ ] **Step 4: Confirm no other references survive**

```bash
grep -rn 'areAllSoundFilesPresent' /mnt/Storage/Projects/keeperfx-workspace/launcher/src/
```

Expected: no output.

- [ ] **Step 5: Build**

```bash
cmake --build /mnt/Storage/Projects/keeperfx-workspace/launcher/build -j"$(nproc)" 2>&1 | tail -8
```

Expected: builds to completion, no errors.

- [ ] **Step 6: Verify case 7 — a FLAC-only folder must not prompt**

```bash
INSTALL=/home/yetipaw/.local/share/keeperfx-alpha
SCRATCH=/tmp/keeperfx-music-test
cp /mnt/Storage/Projects/keeperfx-workspace/launcher/build/keeperfx-launcher-qt "$INSTALL/keeperfx-launcher-qt-test"
mkdir -p "$SCRATCH/backup2" && cp "$INSTALL"/music/*.ogg "$SCRATCH/backup2/"
rm -f "$INSTALL"/music/*.ogg
ffmpeg -loglevel error -y -i "$SCRATCH/backup2/keeper02.ogg" "$INSTALL/music/keeper02.flac"
cd "$INSTALL" && timeout 12 ./keeperfx-launcher-qt-test --allow-multiple >/dev/null 2>&1
grep -i 'Music files not found' "$INSTALL/keeperfx-launcher-qt-test.log" \
  && echo "case 7 FAILED - prompted despite flac present" \
  || echo "case 7 OK - no prompt with flac present"
```

Expected: `case 7 OK - no prompt with flac present`.

- [ ] **Step 7: Verify the empty-folder prompt still fires, then clean up**

```bash
INSTALL=/home/yetipaw/.local/share/keeperfx-alpha
SCRATCH=/tmp/keeperfx-music-test
rm -f "$INSTALL"/music/*.flac
cd "$INSTALL" && timeout 12 ./keeperfx-launcher-qt-test --allow-multiple >/dev/null 2>&1
grep -i 'Music files not found' "$INSTALL/keeperfx-launcher-qt-test.log" \
  && echo "empty-folder prompt OK" || echo "FAILED - no prompt on empty folder"
cp "$SCRATCH/backup2"/*.ogg "$INSTALL/music/"
rm -f "$INSTALL/keeperfx-launcher-qt-test" "$INSTALL/keeperfx-launcher-qt-test.log"
ls "$INSTALL/music/"
```

Expected: `empty-folder prompt OK`, the six oggs restored, and no `-test` artefacts left behind.

- [ ] **Step 8: Commit**

```bash
cd /mnt/Storage/Projects/keeperfx-workspace/launcher
git add src/dkfiles.h src/dkfiles.cpp src/copydkfilesdialog.cpp src/launchermainwindow.cpp
git commit -m "fix(music): treat any playable audio in music/ as music present

The engine now resolves track numbers from whatever audio files exist, so
requiring six exact keeperNN.ogg names would tell a user with a FLAC
soundtrack that their music is missing. areAllSoundFilesPresent() is renamed
to isAnyMusicPresent() because the old name would no longer be true.
DkFiles::musicFiles and the copy loop are unchanged: they read from an
original DK installation, which always uses keeperNN.ogg."
```

---

## Self-Review

**Historical note:** this Self-Review describes Task 1's original implementation. Four fixes landed after this plan was first executed — `ba704e506`/`833928e1c` (sorted-fallback stem dedup, "ADV-4"), `1ebcc3294`/`846e6c6af` (direct stock lookup ahead of the index), and `ea97f4257` (a five-part audit fix, including the numeric/sorted tiebreak) — and changed behaviour this table does not account for. **The design spec (`docs/design/specs/2026-08-03-music-track-detection-design.md`) is the design of record for current behaviour.** The table below is kept as a record of what Task 1 covered when first committed, not a live description of what ships; rows are marked where the code has since moved past them.

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| Cached index built lazily from `music/` | Task 2 Step 2 |
| Enumerate via `LbFileFindFirst`/`Next`/`End` | Task 2 Step 2 |
| Extensions `.ogg`/`.flac`/`.wav`/`.mp3`, case-insensitive | Task 1 (`music_extension_rank`), test 8 |
| Last digit run → candidate track | Task 1 (`music_trailing_number`) |
| Numeric mode if at least one file numbered and every numbered file in 2–7; unnumbered files ignored, out-of-range numbers force fallback | Task 1, tests 1–5, 5b, 5c — **superseded by `ea97f4257`**, which replaced this all-or-nothing rule with the two-candidate, more-tracks-wins tiebreak (see the spec's "Mapping rule" section) |
| Sorted fallback → tracks 2, 3, 4, … | Task 1, tests 3–4, 5c |
| Sorted mode ignores anything past track 7 | Task 1, test 11 |
| Collision FLAC > WAV > OGG > MP3, per track | Task 1, test 6 |
| Same-format clash → sort order | Task 1, test 7 |
| Trailing-number parse does not overflow into a bogus track | Task 1, tests 12–13 |
| Debug notes for dropped collision losers and sorted-mode overflow | Task 1, tests 14–15 |
| **Sorted-fallback stem dedup (including the double-extension case, e.g. `a.ogg`/`a.ogg.flac`)** | *not in this plan's original scope* — added by `ba704e506`/`833928e1c`, extended by `ea97f4257`; see the spec's "Sorted-fallback deduplication" section |
| **Direct stock lookup runs before the index (OGG-first, falls through on an unplayable hit)** | *not in this plan's original scope* — added by `1ebcc3294`/`846e6c6af`, extended by `ea97f4257`; see the spec's "Direct stock lookup is authoritative" section |
| **Numeric-vs-sorted tiebreak: sorted wins only by strictly resolving more tracks** | *not in this plan's original scope* — added by `ea97f4257`; see the spec's "Mapping rule" section |
| Launcher: at least one playable file | Task 3 Steps 1–3 |
| `musicFiles` / copy loop unchanged | Task 3 Step 1 (stated explicitly) |
| Warn once per track | Task 2 Step 3 |
| `play_music_fgroup` / mods untouched | Global Constraints |
| Verification matrix cases 1–8 | Task 2 Steps 5–6, Task 3 Steps 6–7 |

This plan is superseded on every row marked above. It remains a historical record of how the feature was first built and committed across three tasks; it is not a live description of `src/music_index.h` or `play_music_track()` as they stand today. For current behaviour, read the design spec and the code directly — that is also why Task 1's Steps 1 and 3, and Task 2's Step 3, now point at the live source files instead of embedding listings of them.

**Placeholder scan:** none remaining that matter. The three full-source listings this plan used to embed (Task 1 Step 1's test file, Task 1 Step 3's header, Task 2 Step 3's `play_music_track()` diff) drifted from the shipped code — each one twice, independently, despite an explicit in-document warning against exactly that — and have been replaced with pointers to the live files plus a description of what each must do. Every other step still carries a runnable command and its expected output.

**Type consistency:** `build_music_index`, `music_extension_rank`, `music_trailing_number`, `music_to_lower`, `MUSIC_TRACK_MIN`, `MUSIC_TRACK_MAX` are defined in `src/music_index.h` and used with identical names in `src/bflib_sndlib.cpp`. `isAnyMusicPresent()` is declared in `dkfiles.h` and used consistently in `dkfiles.cpp`, `copydkfilesdialog.cpp` and `launchermainwindow.cpp`. `TbFileEntry::Filename` (`bflib_fileio.h`) and `prepare_file_path_buf`/`prepare_file_fmtpath` (`config.h`) are used with matching signatures; line numbers are deliberately not cited here, since they drift with unrelated changes elsewhere in those files.

**One deviation from the spec, deliberate:** the spec placed the logic as "a file-static index in `src/bflib_sndlib.cpp`". This plan puts the pure part in a new header-only `src/music_index.h` instead. Reason: `KFX_SOURCES` in `linux.mk` is an explicit source list and `linux.mk` is upstream-owned, so a new `.cpp` would require editing it (and `Makefile`) against the Global Constraint above. Header-only avoids both build files entirely and makes the logic unit-testable without linking the engine. The stateful cache still lives in `bflib_sndlib.cpp` exactly as the spec describes.
