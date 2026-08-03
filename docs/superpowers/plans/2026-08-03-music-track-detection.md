# Music Track Detection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let any set of audio files dropped into `music/` be picked up as the game's numbered tracks, replacing the hardcoded `keeper%02d.ogg` lookup.

**Architecture:** The track-mapping rule lives in a new header-only file, `src/music_index.h`, with no game, SDL or OpenAL dependency, so it can be unit-tested standalone. `src/bflib_sndlib.cpp` enumerates `music/` once via the existing cross-platform `LbFileFindFirst`/`LbFileFindNext`, feeds the filenames through that pure function, caches the result, and turns `play_music_track()` into a lookup. The launcher's "is music present" check relaxes to match.

**Tech Stack:** C++ (engine, tabs for indentation), C++/Qt 6 (launcher, 4 spaces), GNU make. No test framework exists in either repo; Task 1 introduces a standalone assert-based test binary for the pure logic, and Tasks 2–3 are verified by a manual matrix.

## Global Constraints

- Valid track numbers are **2–7**. 2 is the land view, 3–6 are cycled in-game by `player_utils.c:856` (`3 + (lvnum-1) % 4`), 7 is used by campaigns.
- Recognised extensions: `.ogg`, `.flac`, `.wav`, `.mp3` — compared **case-insensitively** (Linux filesystems are case-sensitive).
- Same-track format preference: **FLAC > WAV > OGG > MP3**, applied per track, never folder-wide.
- **Do not modify `linux.mk` or `Makefile`.** Both are upstream-owned; the header-only design exists specifically to avoid touching them.
- **Do not touch** `play_music_fgroup()`, `find_music_file_for_mod_list()`, or the `SET_MUSIC` script path. Those already accept arbitrary filenames and are a separate mechanism.
- Engine C++ files use **tab** indentation; launcher files use **4 spaces**. Match the file you are editing.
- A test build of the engine must **not** be left installed in `~/.local/share/keeperfx-alpha/` unless built with `BUILD_NUMBER`/`VER_SUFFIX`, or its `1.4.0.0` version string confuses the version-gated self-update.
- Build the engine with `make -f linux.mk -j"$(nproc)"` (or bare `make`, which the fork's `GNUmakefile` forwards). Build the launcher with `cmake --build /home/yetipaw/.cache/launcher-qt/build -j"$(nproc)"`.

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
  - `std::map<int, std::string> build_music_index(const std::vector<std::string> & entries)` — takes bare filenames from a directory listing, returns track number → filename.
  - `int music_extension_rank(const std::string & fname)` — index into the preference list, `-1` if not audio.
  - `int music_trailing_number(const std::string & fname)` — last digit run in the stem, `-1` if none.
  - Constants `MUSIC_TRACK_MIN` (2) and `MUSIC_TRACK_MAX` (7).

- [ ] **Step 1: Write the failing test**

Create `tests/test_music_index.cpp`:

```cpp
// Standalone unit tests for the pure music track-mapping logic.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -o bin/test_music_index tests/test_music_index.cpp
//   ./bin/test_music_index
#include "../src/music_index.h"

#include <cstdio>

static int g_failures = 0;

static void expect_track(const std::map<int, std::string> & index, int track,
	const char * expected, const char * what)
{
	std::map<int, std::string>::const_iterator it = index.find(track);
	const bool ok = (it != index.end()) && (it->second == expected);
	if (ok) {
		std::printf("  ok   %s\n", what);
	} else {
		std::printf("  FAIL %s: track %d -> '%s', wanted '%s'\n", what, track,
			(it == index.end()) ? "<missing>" : it->second.c_str(), expected);
		++g_failures;
	}
}

static void expect_size(const std::map<int, std::string> & index, size_t expected,
	const char * what)
{
	if (index.size() == expected) {
		std::printf("  ok   %s\n", what);
	} else {
		std::printf("  FAIL %s: size %d, wanted %d\n", what,
			(int)index.size(), (int)expected);
		++g_failures;
	}
}

int main()
{
	// 1. Stock install: numeric mode, resolves exactly as the old hardcoded path did.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg");
		in.push_back("keeper04.ogg"); in.push_back("keeper05.ogg");
		in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "stock ogg set yields 6 tracks");
		expect_track(got, 2, "keeper02.ogg", "stock ogg track 2");
		expect_track(got, 7, "keeper07.ogg", "stock ogg track 7");
	}

	// 2. FLAC set with spaces in the names, still numeric.
	{
		std::vector<std::string> in;
		in.push_back("Track 02.flac"); in.push_back("Track 03.flac");
		in.push_back("Track 04.flac"); in.push_back("Track 05.flac");
		in.push_back("Track 06.flac"); in.push_back("Track 07.flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "flac set yields 6 tracks");
		expect_track(got, 3, "Track 03.flac", "flac track 3");
	}

	// 3. Numbered from zero: out of range, so sorted fallback maps to 2..7.
	{
		std::vector<std::string> in;
		in.push_back("music00.ogg"); in.push_back("music01.ogg");
		in.push_back("music02.ogg"); in.push_back("music03.ogg");
		in.push_back("music04.ogg"); in.push_back("music05.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "zero-based set yields 6 tracks");
		expect_track(got, 2, "music00.ogg", "zero-based first file becomes track 2");
		expect_track(got, 7, "music05.ogg", "zero-based last file becomes track 7");
	}

	// 4. Numbered from one: 1 is out of range, so sorted fallback again.
	{
		std::vector<std::string> in;
		in.push_back("audiocd01.mp3"); in.push_back("audiocd02.mp3");
		in.push_back("audiocd03.mp3"); in.push_back("audiocd04.mp3");
		in.push_back("audiocd05.mp3"); in.push_back("audiocd06.mp3");
		const std::map<int, std::string> got = build_music_index(in);
		expect_track(got, 2, "audiocd01.mp3", "one-based first file becomes track 2");
	}

	// 5. One unnumbered file means the names cannot describe a complete set.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("bonus.flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 2, "mixed numbered/unnumbered yields 2 tracks");
		expect_track(got, 2, "bonus.flac", "sorted fallback puts bonus.flac first");
		expect_track(got, 3, "keeper02.ogg", "sorted fallback puts keeper02.ogg second");
	}

	// 6. Same track in two formats: lossless wins, others unaffected.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg");
		in.push_back("keeper03.flac"); in.push_back("keeper04.ogg");
		in.push_back("keeper05.ogg"); in.push_back("keeper06.ogg");
		in.push_back("keeper07.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "format collision still yields 6 tracks");
		expect_track(got, 3, "keeper03.flac", "flac beats ogg for track 3");
		expect_track(got, 2, "keeper02.ogg", "collision leaves track 2 alone");
	}

	// 7. Same track, same format: earlier in sort order wins.
	{
		std::vector<std::string> in;
		in.push_back("track03.ogg"); in.push_back("keeper03.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_track(got, 3, "keeper03.ogg", "same-format clash resolves by sort order");
	}

	// 8. Uppercase names and extensions are recognised.
	{
		std::vector<std::string> in;
		in.push_back("KEEPER02.OGG"); in.push_back("KEEPER03.OGG");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 2, "uppercase names are recognised");
		expect_track(got, 2, "KEEPER02.OGG", "uppercase track 2");
	}

	// 9. Non-audio entries are ignored entirely.
	{
		std::vector<std::string> in;
		in.push_back("MusicReadme.txt"); in.push_back("keeper02.ogg");
		in.push_back("keeper03.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 2, "non-audio entries ignored");
	}

	// 10. Empty folder yields an empty index rather than anything spurious.
	{
		const std::vector<std::string> in;
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 0, "empty folder yields empty index");
	}

	// 11. Sorted mode stops at track 7 instead of inventing higher tracks.
	{
		std::vector<std::string> in;
		for (int i = 0; i < 10; ++i) {
			in.push_back(std::string("a") + (char)('0' + i) + ".ogg");
		}
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "sorted mode caps at track 7");
		expect_track(got, 7, "a5.ogg", "sixth file becomes track 7");
	}

	if (g_failures == 0) {
		std::printf("\nAll music index tests passed.\n");
		return 0;
	}
	std::printf("\n%d music index test(s) FAILED.\n", g_failures);
	return 1;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /mnt/Storage/Projects/keeperfx-alpha
mkdir -p bin
g++ -std=c++17 -Wall -Wextra -o bin/test_music_index tests/test_music_index.cpp
```

Expected: FAILS to compile with `fatal error: ../src/music_index.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `src/music_index.h`:

```cpp
#pragma once
/******************************************************************************/
// Pure track-mapping logic for the music/ folder.
//
// Header-only, and free of any game, SDL or OpenAL dependency, so that
// tests/test_music_index.cpp can exercise it without linking the engine. It is
// also why this lives outside bflib_sndlib.cpp: neither linux.mk nor Makefile
// needs a new entry, and both are upstream-owned.
/******************************************************************************/
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Track numbers the game actually asks for: 2 is the land view, 3-6 are cycled
// in-game by player_utils.c, and 7 is used by campaigns.
static const int MUSIC_TRACK_MIN = 2;
static const int MUSIC_TRACK_MAX = 7;

// Recognised extensions, in preference order for a same-track collision:
// lossless first, then the better lossy codec.
static const char * const MUSIC_EXTENSIONS[] = { ".flac", ".wav", ".ogg", ".mp3" };
static const int MUSIC_EXTENSION_COUNT = 4;

inline std::string music_to_lower(std::string text) {
	for (std::string::size_type i = 0; i < text.size(); ++i) {
		text[i] = (char)tolower((unsigned char)text[i]);
	}
	return text;
}

// Index into MUSIC_EXTENSIONS (lower is preferred), or -1 when not an audio file.
inline int music_extension_rank(const std::string & fname) {
	const std::string lower = music_to_lower(fname);
	for (int i = 0; i < MUSIC_EXTENSION_COUNT; ++i) {
		const std::string ext(MUSIC_EXTENSIONS[i]);
		if (lower.size() > ext.size()
			&& lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
			return i;
		}
	}
	return -1;
}

// Last run of digits in the basename, extension stripped. -1 when there is none.
inline int music_trailing_number(const std::string & fname) {
	const std::string::size_type dot = fname.find_last_of('.');
	const std::string stem = (dot == std::string::npos) ? fname : fname.substr(0, dot);
	std::string::size_type end = std::string::npos;
	for (std::string::size_type i = stem.size(); i-- > 0; ) {
		if (isdigit((unsigned char)stem[i])) {
			end = i + 1;
			break;
		}
	}
	if (end == std::string::npos) {
		return -1;
	}
	std::string::size_type begin = end;
	while (begin > 0 && isdigit((unsigned char)stem[begin - 1])) {
		--begin;
	}
	return atoi(stem.substr(begin, end - begin).c_str());
}

// Maps track number -> filename for the given directory contents. Entries are
// bare filenames, not paths; anything that is not a recognised audio file is
// ignored.
inline std::map<int, std::string> build_music_index(const std::vector<std::string> & entries) {
	// Keep the audio files, remembering each one's format preference.
	std::vector<std::pair<std::string, int> > files;
	for (std::size_t i = 0; i < entries.size(); ++i) {
		const int rank = music_extension_rank(entries[i]);
		if (rank >= 0) {
			files.push_back(std::make_pair(entries[i], rank));
		}
	}

	// Ascending by filename, case-insensitive. The raw name breaks ties so the
	// ordering is total and the result reproducible across filesystems.
	std::sort(files.begin(), files.end(),
		[](const std::pair<std::string, int> & a, const std::pair<std::string, int> & b) {
			const std::string la = music_to_lower(a.first);
			const std::string lb = music_to_lower(b.first);
			return (la != lb) ? (la < lb) : (a.first < b.first);
		});

	// Numeric mode needs every file to carry a number AND every one of those
	// numbers to be a track the game asks for. Anything else means the filenames
	// cannot describe a complete set, so fall back to sorted order rather than
	// producing a half-populated index.
	bool numeric = !files.empty();
	for (std::size_t i = 0; numeric && i < files.size(); ++i) {
		const int track = music_trailing_number(files[i].first);
		if (track < MUSIC_TRACK_MIN || track > MUSIC_TRACK_MAX) {
			numeric = false;
		}
	}

	std::map<int, std::string> index;
	if (numeric) {
		std::map<int, int> best_rank;
		for (std::size_t i = 0; i < files.size(); ++i) {
			const int track = music_trailing_number(files[i].first);
			const std::map<int, int>::iterator seen = best_rank.find(track);
			// Strictly-better keeps the first of an equal-ranked pair, so a
			// same-format clash resolves to whichever sorts first.
			if (seen == best_rank.end() || files[i].second < seen->second) {
				index[track] = files[i].first;
				best_rank[track] = files[i].second;
			}
		}
	} else {
		int track = MUSIC_TRACK_MIN;
		for (std::size_t i = 0; i < files.size() && track <= MUSIC_TRACK_MAX; ++i) {
			index[track] = files[i].first;
			++track;
		}
	}
	return index;
}
```

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
- Modify: `src/bflib_sndlib.cpp` (includes near line 7; globals near line 54; `play_music_track()` at line 778)

**Interfaces:**
- Consumes: `build_music_index()`, `MUSIC_TRACK_MIN`, `MUSIC_TRACK_MAX` from Task 1. `LbFileFindFirst`/`LbFileFindNext`/`LbFileFindEnd` and `struct TbFileEntry { const char * Filename; }` from `bflib_fileio.h` (already included at line 7). `prepare_file_path_buf(char *dst, int dst_size, short fgroup, const char *fname)` and `prepare_file_fmtpath(short fgroup, const char *fmt_str, ...)`, both already used in this file.
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
std::map<int, std::string> g_music_index;
bool g_music_index_built = false;
std::vector<int> g_music_warned_tracks;

const std::map<int, std::string> & music_index() {
	if (!g_music_index_built) {
		g_music_index_built = true;
		std::vector<std::string> entries;
		char spec[2048];
		prepare_file_path_buf(spec, sizeof(spec), FGrp_Music, "*");
		struct TbFileEntry fe;
		struct TbFileFind * ff = LbFileFindFirst(spec, &fe);
		if (ff) {
			do {
				entries.push_back(fe.Filename);
			} while (LbFileFindNext(ff, &fe) >= 0);
			LbFileFindEnd(ff);
		}
		g_music_index = build_music_index(entries);
		SYNCDBG(7, "Music index built: %d playable track(s) from %d directory entr(ies)",
			(int)g_music_index.size(), (int)entries.size());
	}
	return g_music_index;
}
```

- [ ] **Step 3: Rewrite the disk branch of `play_music_track()`**

In `src/bflib_sndlib.cpp`, replace this existing branch:

```cpp
	} else if (features_enabled & Ft_NoCdMusic) {
		// play_music() itself skips restarting if this exact resolved file is
		// already the one actually playing (e.g. reloading a save for the same level).
		return play_music(prepare_file_fmtpath(FGrp_Music, "keeper%02d.ogg", track));
	} else {
```

with:

```cpp
	} else if (features_enabled & Ft_NoCdMusic) {
		const std::map<int, std::string> & index = music_index();
		const std::map<int, std::string>::const_iterator it = index.find(track);
		if (it == index.end()) {
			// Warn once per track: a level that retries must not flood the log,
			// but a silent music folder should never again be undiagnosable.
			if (std::find(g_music_warned_tracks.begin(), g_music_warned_tracks.end(), track)
				== g_music_warned_tracks.end()) {
				g_music_warned_tracks.push_back(track);
				WARNLOG("No music file for track %d; the music folder supplied %d playable track(s)",
					track, (int)index.size());
			}
			return false;
		}
		// play_music() itself skips restarting if this exact resolved file is
		// already the one actually playing (e.g. reloading a save for the same level).
		return play_music(prepare_file_fmtpath(FGrp_Music, "%s", it->second.c_str()));
	} else {
```

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
SCRATCH=/mnt/Storage/tmp/claude-1000/-mnt-Storage-Projects-keeperfx-alpha/music-test
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
- Modify: `/home/yetipaw/.cache/launcher-qt/src/dkfiles.h:20`
- Modify: `/home/yetipaw/.cache/launcher-qt/src/dkfiles.cpp` (`areAllSoundFilesPresent()`)
- Modify: `/home/yetipaw/.cache/launcher-qt/src/copydkfilesdialog.cpp:120`
- Modify: `/home/yetipaw/.cache/launcher-qt/src/launchermainwindow.cpp` (the missing-music startup check)

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
    // Do not add QDir::NoSymLinks here for symmetry with the other functions.
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
grep -rn 'areAllSoundFilesPresent' /home/yetipaw/.cache/launcher-qt/src/
```

Expected: no output.

- [ ] **Step 5: Build**

```bash
cmake --build /home/yetipaw/.cache/launcher-qt/build -j"$(nproc)" 2>&1 | tail -8
```

Expected: builds to completion, no errors.

- [ ] **Step 6: Verify case 7 — a FLAC-only folder must not prompt**

```bash
INSTALL=/home/yetipaw/.local/share/keeperfx-alpha
SCRATCH=/mnt/Storage/tmp/claude-1000/-mnt-Storage-Projects-keeperfx-alpha/music-test
cp /home/yetipaw/.cache/launcher-qt/build/keeperfx-launcher-qt "$INSTALL/keeperfx-launcher-qt-test"
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
SCRATCH=/mnt/Storage/tmp/claude-1000/-mnt-Storage-Projects-keeperfx-alpha/music-test
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
cd /home/yetipaw/.cache/launcher-qt
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

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| Cached index built lazily from `music/` | Task 2 Step 2 |
| Enumerate via `LbFileFindFirst`/`Next`/`End` | Task 2 Step 2 |
| Extensions `.ogg`/`.flac`/`.wav`/`.mp3`, case-insensitive | Task 1 (`music_extension_rank`), test 8 |
| Last digit run → candidate track | Task 1 (`music_trailing_number`) |
| Numeric mode only if every file numbered and all in 2–7 | Task 1, tests 1–5 |
| Sorted fallback → tracks 2, 3, 4, … | Task 1, tests 3–5 |
| Sorted mode ignores anything past track 7 | Task 1, test 11 |
| Collision FLAC > WAV > OGG > MP3, per track | Task 1, test 6 |
| Same-format clash → sort order | Task 1, test 7 |
| Launcher: at least one playable file | Task 3 Steps 1–3 |
| `musicFiles` / copy loop unchanged | Task 3 Step 1 (stated explicitly) |
| Warn once per track | Task 2 Step 3 |
| `play_music_fgroup` / mods untouched | Global Constraints |
| Verification matrix cases 1–8 | Task 2 Steps 5–6, Task 3 Steps 6–7 |

No gaps.

**Placeholder scan:** none. Every code step carries complete code; every verification step carries a runnable command and its expected output.

**Type consistency:** `build_music_index`, `music_extension_rank`, `music_trailing_number`, `music_to_lower`, `MUSIC_TRACK_MIN`, `MUSIC_TRACK_MAX` are defined in Task 1 and used with identical names and signatures in Task 2. `isAnyMusicPresent()` is declared in Task 3 Step 1 and used in Steps 2–3 consistently. `TbFileEntry::Filename` matches `bflib_fileio.h:44`. `prepare_file_path_buf` and `prepare_file_fmtpath` match `config.h:269` and `config.h:271`.

**One deviation from the spec, deliberate:** the spec placed the logic as "a file-static index in `src/bflib_sndlib.cpp`". This plan puts the pure part in a new header-only `src/music_index.h` instead. Reason: `KFX_SOURCES` in `linux.mk` is an explicit source list and `linux.mk` is upstream-owned, so a new `.cpp` would require editing it (and `Makefile`) against the Global Constraint above. Header-only avoids both build files entirely and makes the logic unit-testable without linking the engine. The stateful cache still lives in `bflib_sndlib.cpp` exactly as the spec describes.
