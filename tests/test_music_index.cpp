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

	// 5. FIX 2 tiebreak: one unnumbered file alongside one numbered file.
	// Numeric only resolves the numbered file (1 track); sorted resolves
	// both (2 tracks), so sorted strictly resolves more and wins -- per the
	// audit table row "keeper02.ogg + bonus.flac -> 1/2 -> sorted". This is
	// a CHANGED expectation: before FIX 2's "more tracks wins" tiebreak, the
	// old all-or-nothing numeric/sorted decision kept this numeric (the
	// unnumbered file was simply excluded, not counted against it), so this
	// used to assert size 1 with keeper02.ogg on track 2. Both files are
	// genuinely playable, so resolving both is strictly better here; the
	// real regression guard (a stray file must never cost a *complete*
	// numbered set any of its tracks) is covered by 5b below, where the
	// tie still favours numeric.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("bonus.flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 2, "FIX 2: lone numbered file plus one unnumbered file -- sorted resolves both");
		expect_track(got, 2, "bonus.flac", "FIX 2: sorted fallback, bonus.flac sorts first");
		expect_track(got, 3, "keeper02.ogg", "FIX 2: sorted fallback, keeper02.ogg sorts second");
	}

	// 5b. A complete stock set plus one stray unnumbered file: the exact
	// regression FIX 1 exists to prevent. All six numbered tracks must be
	// completely unaffected; the stray file is simply dropped.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg");
		in.push_back("keeper04.ogg"); in.push_back("keeper05.ogg");
		in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		in.push_back("bonus.mp3");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "stock six plus a stray file still yields exactly 6 tracks");
		expect_track(got, 2, "keeper02.ogg", "stock six plus stray: track 2 unaffected");
		expect_track(got, 7, "keeper07.ogg", "stock six plus stray: track 7 unaffected");
	}

	// 5d. FIX 2 audit table row: a complete stock set plus a stray file whose
	// own number (01) is out of range, rather than merely absent. Numeric
	// resolves the 6 in-range files (the stray is excluded, not counted
	// against it); sorted resolves 6 too (7 distinct stems capped at 6); the
	// tie favours numeric, so the stray is ignored and the set is
	// unaffected -- exactly the same protection as 5b, for an
	// out-of-range-numbered stray instead of an unnumbered one.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg");
		in.push_back("keeper04.ogg"); in.push_back("keeper05.ogg");
		in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		in.push_back("bonus01.mp3");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "FIX 2: stock six plus an out-of-range-numbered stray still yields 6 tracks");
		expect_track(got, 2, "keeper02.ogg", "FIX 2: stray with out-of-range number, track 2 unaffected");
		expect_track(got, 7, "keeper07.ogg", "FIX 2: stray with out-of-range number, track 7 unaffected");
	}

	// 5e. FIX 2 audit table row: same as 5d but with FLAC names with spaces,
	// and using Track 02.flac's numbering scheme.
	{
		std::vector<std::string> in;
		in.push_back("Track 02.flac"); in.push_back("Track 03.flac");
		in.push_back("Track 04.flac"); in.push_back("Track 05.flac");
		in.push_back("Track 06.flac"); in.push_back("Track 07.flac");
		in.push_back("bonus01.mp3");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "FIX 2: flac six plus an out-of-range-numbered stray still yields 6 tracks");
		expect_track(got, 2, "Track 02.flac", "FIX 2: flac six plus stray, track 2 unaffected");
		expect_track(got, 7, "Track 07.flac", "FIX 2: flac six plus stray, track 7 unaffected");
	}

	// 5f. FIX 2 audit table row: a complete set plus a near-duplicate whose
	// own trailing number is out of range ("Track 02 (1).flac" parses as
	// track 1, from the "(1)" -- see music_trailing_number()'s rightmost-
	// digit-run rule). It is excluded from the numeric candidate exactly
	// like 5d/5e's stray, so it is "ignored" rather than colliding with
	// Track 02.flac.
	{
		std::vector<std::string> in;
		in.push_back("Track 02.flac"); in.push_back("Track 03.flac");
		in.push_back("Track 04.flac"); in.push_back("Track 05.flac");
		in.push_back("Track 06.flac"); in.push_back("Track 07.flac");
		in.push_back("Track 02 (1).flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "FIX 2: Track 02 (1).flac ignored, still yields 6 tracks");
		expect_track(got, 2, "Track 02.flac", "FIX 2: the (1) duplicate does not steal track 2");
	}

	// 5g. FIX 2 audit table row: a single stock file alone. Numeric and
	// sorted both resolve exactly 1 track, so the tie favours numeric.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 1, "FIX 2: a single file alone resolves via the numeric/sorted tie");
		expect_track(got, 2, "keeper02.ogg", "FIX 2: lone file keeps its own track number");
	}

	// 5c. No file numbered at all: sorted fallback assigns tracks in name
	// order, same as before this fix.
	{
		std::vector<std::string> in;
		in.push_back("dungeon.flac"); in.push_back("battle.flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 2, "no numbered files at all yields sorted fallback");
		expect_track(got, 2, "battle.flac", "sorted fallback: battle.flac sorts first");
		expect_track(got, 3, "dungeon.flac", "sorted fallback: dungeon.flac sorts second");
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

	// 7. Same track, same format: earlier in sort order wins. Embedded in a
	// full 6-track set (rather than just the two colliding files alone) so
	// numeric resolves all 6 tracks and ties with what sorted would resolve
	// (6 distinct stems capped at 6) -- numeric wins the tie, so the
	// collision rule below actually gets exercised. CHANGED under FIX 2: the
	// original version of this test used only the two colliding files
	// (track03.ogg + keeper03.ogg) with no other tracks; under FIX 2's
	// "more tracks wins" tiebreak, sorted resolves both of those (2 stems,
	// since "track03" and "keeper03" are different stems) while numeric
	// resolves only 1 (the collision collapses them to a single track), so
	// sorted now wins that bare two-file case instead -- see test 5's
	// comment for the same effect. That is a real, separate consequence of
	// FIX 2, not a bug in the collision rule itself.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg"); in.push_back("keeper04.ogg");
		in.push_back("keeper05.ogg"); in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		in.push_back("track03.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "same-format clash: full set still yields 6 tracks");
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
	// CHANGED under FIX 2: the original fixture was "a0.ogg".."a9.ogg", whose
	// trailing digits happen to give a2..a7 valid in-range track numbers, so
	// numeric now resolves 6 tracks (a2..a7) and ties with what sorted would
	// resolve (6, capped) -- numeric wins the tie, so that fixture no longer
	// exercises sorted mode's cap at all. Renamed to files with no trailing
	// digits so numeric resolves 0 and sorted is guaranteed to win, keeping
	// this test's original intent (the cap itself) meaningful.
	{
		std::vector<std::string> in;
		for (int i = 0; i < 10; ++i) {
			in.push_back(std::string("song-") + (char)('a' + i) + ".ogg");
		}
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "sorted mode caps at track 7");
		expect_track(got, 7, "song-f.ogg", "sixth file becomes track 7");
	}

	// 12. music_trailing_number() must not let a huge digit run overflow into
	// a bogus small track (formerly atoi()'s UB, e.g. track4294967298.ogg
	// silently parsing as track 2).
	{
		const int got1 = music_trailing_number("track4294967298.ogg");           // overflows int, fits in a 64-bit long
		const int got2 = music_trailing_number("track99999999999999999999.ogg"); // overflows long outright
		if (got1 == -1 && got2 == -1) {
			std::printf("  ok   absurdly large trailing numbers do not overflow into a bogus track\n");
		} else {
			std::printf("  FAIL absurdly large trailing numbers: got %d / %d, wanted -1 / -1\n", got1, got2);
			++g_failures;
		}
	}

	// 13. A set of only absurdly-numbered files must fall back to sorted
	// mode, not produce a bogus numeric mapping.
	{
		std::vector<std::string> in;
		in.push_back("aaa4294967298.ogg");
		in.push_back("bbb99999999999999999999.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 2, "a set of absurdly-numbered files falls back to sorted mode");
		expect_track(got, 2, "aaa4294967298.ogg", "absurd set, sorted fallback first file");
		expect_track(got, 3, "bbb99999999999999999999.ogg", "absurd set, sorted fallback second file");
	}

	// 14. Notes: a same-format collision records a note for the dropped file.
	// Uses the same full-6-track fixture as test 7 (see its comment): the
	// bare two-file collision alone now resolves via sorted mode with no
	// drops at all under FIX 2, so it no longer exercises a collision note.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg"); in.push_back("keeper04.ogg");
		in.push_back("keeper05.ogg"); in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		in.push_back("track03.ogg");
		std::vector<std::string> notes;
		build_music_index(in, &notes);
		if (!notes.empty()) {
			std::printf("  ok   same-format collision records a note\n");
		} else {
			std::printf("  FAIL same-format collision recorded no notes\n");
			++g_failures;
		}
	}

	// 15. Notes: sorted mode discarding files past track 7 records a note
	// per dropped file. Uses the same no-trailing-digit fixture as test 11
	// (see its comment): "a0.ogg".."a9.ogg" now resolves via numeric mode
	// under FIX 2 (a2..a7 tie with sorted's capped 6), so it no longer
	// exercises sorted mode's overflow-past-track-7 path -- it happened to
	// still report non-empty notes either way (numeric's own out-of-range
	// notes for a0/a1/a8/a9), which is why this test did not fail, but it
	// was no longer testing what its comment claims.
	{
		std::vector<std::string> in;
		for (int i = 0; i < 10; ++i) {
			in.push_back(std::string("song-") + (char)('a' + i) + ".ogg");
		}
		std::vector<std::string> notes;
		build_music_index(in, &notes);
		if (!notes.empty()) {
			std::printf("  ok   sorted-mode overflow past track 7 records a note\n");
		} else {
			std::printf("  FAIL sorted-mode overflow past track 7 recorded no notes\n");
			++g_failures;
		}
	}

	// 16. FIX A: numeric mode silently dropped every unnumbered file with no
	// trace in notes. A curated folder with one numbered file among several
	// untitled tracks used to collapse to just that one numbered track.
	// CHANGED under FIX 2: numeric now only resolves 1 track here (the file
	// with "2" in its name) while sorted resolves all 6 (no duplicate stems,
	// exactly enough room), so sorted strictly resolves more and wins --
	// per the audit table row "curated 6 unnumbered + Dungeon Keeper 2.flac
	// -> 1/6 -> sorted (fixes the curated-folder collapse)". The folder is
	// no longer collapsed to one track and, since sorted mode has no drops
	// to explain here, no notes are produced.
	{
		std::vector<std::string> in;
		in.push_back("Ambience.ogg"); in.push_back("Battle Theme.ogg");
		in.push_back("Dungeon Keeper 2.ogg"); in.push_back("Menu.ogg");
		in.push_back("Victory.ogg"); in.push_back("War Drums.ogg");
		std::vector<std::string> notes;
		const std::map<int, std::string> got = build_music_index(in, &notes);
		expect_size(got, 6, "FIX 2: curated folder no longer collapses -- all six tracks resolve");
		expect_track(got, 2, "Ambience.ogg", "FIX 2: curated folder, sorted order track 2");
		expect_track(got, 4, "Dungeon Keeper 2.ogg", "FIX 2: curated folder, sorted order track 4");
		expect_track(got, 7, "War Drums.ogg", "FIX 2: curated folder, sorted order track 7");
		if (notes.empty()) {
			std::printf("  ok   curated folder: sorted mode has no drops to note\n");
		} else {
			std::printf("  FAIL curated folder: got %d notes, wanted 0\n", (int)notes.size());
			++g_failures;
		}
	}

	// 17. FIX B: macOS AppleDouble sidecars ("._keeper02.ogg" etc.) sort
	// before their real counterparts and must not be allowed to win the
	// collision -- they must not even be candidates.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg");
		in.push_back("keeper04.ogg"); in.push_back("keeper05.ogg");
		in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		in.push_back("._keeper02.ogg"); in.push_back("._keeper03.ogg");
		in.push_back("._keeper04.ogg"); in.push_back("._keeper05.ogg");
		in.push_back("._keeper06.ogg"); in.push_back("._keeper07.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 6, "AppleDouble sidecars: still exactly 6 tracks");
		expect_track(got, 2, "keeper02.ogg", "AppleDouble sidecars: real file wins track 2");
		expect_track(got, 7, "keeper07.ogg", "AppleDouble sidecars: real file wins track 7");
	}

	// 18. A lone dotfile is not music, AppleDouble or otherwise.
	{
		std::vector<std::string> in;
		in.push_back(".hidden.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 0, "a lone dotfile is not treated as music");
	}

	// 19. ADV-4: sorted fallback must not double up a song that exists in two
	// formats. A "01"-style prefix is out of the 2-7 range, so this whole
	// library forces sorted mode; without stem dedup, the four songs would
	// spread across all six tracks (each song claiming two, one song
	// dropped entirely) instead of resolving to four distinct FLAC tracks.
	{
		std::vector<std::string> in;
		in.push_back("01 intro.flac"); in.push_back("01 intro.ogg");
		in.push_back("02 dungeon.flac"); in.push_back("02 dungeon.ogg");
		in.push_back("03 battle.flac"); in.push_back("03 battle.ogg");
		in.push_back("04 boss.flac"); in.push_back("04 boss.ogg");
		std::vector<std::string> notes;
		const std::map<int, std::string> got = build_music_index(in, &notes);
		expect_size(got, 4, "ADV-4: FLAC+OGG library collapses to 4 distinct songs");
		expect_track(got, 2, "01 intro.flac", "ADV-4: track 2 is the FLAC intro");
		expect_track(got, 3, "02 dungeon.flac", "ADV-4: track 3 is the FLAC dungeon theme");
		expect_track(got, 4, "03 battle.flac", "ADV-4: track 4 is the FLAC battle theme");
		expect_track(got, 5, "04 boss.flac", "ADV-4: track 5 is the FLAC boss theme");
		if (notes.size() == 4) {
			std::printf("  ok   ADV-4: one dedup note per dropped OGG duplicate\n");
		} else {
			std::printf("  FAIL ADV-4: got %d notes, wanted 4\n", (int)notes.size());
			++g_failures;
		}
	}

	// 20. ADV-4: rank must beat alphabetical sort order, not just coincide
	// with it. Note this deliberately uses WAV vs MP3/OGG rather than the
	// FLAC vs MP3 pairing one might reach for first: ".flac" sorts
	// alphabetically before every other recognised extension (f < m/o/w), so
	// FLAC would win any such pairing purely by sorting first -- that would
	// not actually prove rank is consulted at all. WAV is the one format
	// whose rank preference (2nd, beating OGG and MP3) is the *opposite* of
	// its alphabetical position ("wav" sorts after both "mp3" and "ogg"), so
	// it is the only pairing that can distinguish "picks by rank" from
	// "picks whichever sorts first".
	{
		std::vector<std::string> in;
		in.push_back("song.mp3"); in.push_back("song.wav");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 1, "ADV-4: mp3+wav of the same song collapses to one track");
		expect_track(got, 2, "song.wav", "ADV-4: WAV wins over MP3 despite sorting later");
	}

	// 21. ADV-4: three formats of the same song all collapse to a single
	// entry, and the lossless one wins.
	{
		std::vector<std::string> in;
		in.push_back("x.mp3"); in.push_back("x.ogg"); in.push_back("x.flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 1, "ADV-4: three formats of one song collapse to one entry");
		expect_track(got, 2, "x.flac", "ADV-4: FLAC wins the three-way collapse");
	}

	// 22. ADV-4 regression guard: a sorted-mode set with no duplicate stems
	// maps exactly as it did before the dedup change.
	{
		std::vector<std::string> in;
		in.push_back("dungeon.flac"); in.push_back("battle.ogg"); in.push_back("boss.mp3");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 3, "ADV-4: no-duplicate-stems set is unaffected by dedup");
		expect_track(got, 2, "battle.ogg", "ADV-4: no-dup set, track 2 unchanged");
		expect_track(got, 3, "boss.mp3", "ADV-4: no-dup set, track 3 unchanged");
		expect_track(got, 4, "dungeon.flac", "ADV-4: no-dup set, track 4 unchanged");
	}

	// 23. FIX 5: an extra, embedded recognised-extension-looking token before
	// the real extension (e.g. a file re-exported without renaming, so its
	// old extension is still part of the name) must not contaminate the
	// trailing-digit search. "keeper02.mp3.ogg" used to parse as track 3,
	// from the "3" in the leftover ".mp3", instead of track 2.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.mp3.ogg");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 1, "FIX 5: double-extension file is still recognised");
		expect_track(got, 2, "keeper02.mp3.ogg", "FIX 5: keeper02.mp3.ogg parses as track 2, not 3");
	}

	// 24. FIX 5: sorted-fallback stem dedup treats "a.ogg" and "a.ogg.flac"
	// as the same song (same reasoning as test 23 -- music_strip_extensions()
	// strips both trailing recognised extensions), so the FLAC copy wins
	// and the OGG is dropped as a duplicate rather than claiming its own
	// track.
	{
		std::vector<std::string> in;
		in.push_back("a.ogg"); in.push_back("a.ogg.flac");
		std::vector<std::string> notes;
		const std::map<int, std::string> got = build_music_index(in, &notes);
		expect_size(got, 1, "FIX 5: a.ogg and a.ogg.flac dedup to the same song");
		expect_track(got, 2, "a.ogg.flac", "FIX 5: the FLAC copy wins the dedup");
		if (notes.size() == 1) {
			std::printf("  ok   FIX 5: one dedup note for the dropped a.ogg\n");
		} else {
			std::printf("  FAIL FIX 5: got %d notes, wanted 1\n", (int)notes.size());
			++g_failures;
		}
	}

	// 25. FIX 5: an equal-rank, case-only duplicate in sorted-fallback mode
	// (same format, different case) must not be reported as a "higher-
	// preference format" -- the two files are the *same* format, so the
	// note must say the survivor simply sorts first.
	{
		std::vector<std::string> in;
		in.push_back("Song.OGG"); in.push_back("song.ogg");
		std::vector<std::string> notes;
		const std::map<int, std::string> got = build_music_index(in, &notes);
		expect_size(got, 1, "FIX 5: case-only duplicate dedups to one song");
		expect_track(got, 2, "Song.OGG", "FIX 5: case-only duplicate, earlier sort order wins");
		bool ok = notes.size() == 1
			&& notes[0].find("higher-preference") == std::string::npos
			&& notes[0].find("sorts first") != std::string::npos;
		if (ok) {
			std::printf("  ok   FIX 5: case-only duplicate note does not claim a format difference\n");
		} else {
			std::printf("  FAIL FIX 5: case-only duplicate note wording: got %s\n",
				notes.empty() ? "<no notes>" : notes[0].c_str());
			++g_failures;
		}
	}

	// 26. FIX 5: an overflowed track number must be reported as "too large",
	// not as "no track number found" -- the file does have a number, it is
	// just implausible. Embedded in a full 6-track set so numeric wins the
	// tiebreak and this file's own drop note (from the numeric candidate) is
	// the one that actually gets surfaced.
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("keeper03.ogg");
		in.push_back("keeper04.ogg"); in.push_back("keeper05.ogg");
		in.push_back("keeper06.ogg"); in.push_back("keeper07.ogg");
		in.push_back("track4294967298.ogg");
		std::vector<std::string> notes;
		const std::map<int, std::string> got = build_music_index(in, &notes);
		expect_size(got, 6, "FIX 5: overflowed stray does not affect the numeric six");
		bool found_correct_note = false;
		bool found_wrong_note = false;
		for (std::size_t i = 0; i < notes.size(); ++i) {
			if (notes[i].find("too large") != std::string::npos) {
				found_correct_note = true;
			}
			if (notes[i].find("no track number") != std::string::npos) {
				found_wrong_note = true;
			}
		}
		if (found_correct_note && !found_wrong_note) {
			std::printf("  ok   FIX 5: overflowed number reported as too large, not as no number\n");
		} else {
			std::printf("  FAIL FIX 5: overflow note wording wrong (correct=%d, wrong=%d)\n",
				(int)found_correct_note, (int)found_wrong_note);
			++g_failures;
		}
	}

	if (g_failures == 0) {
		std::printf("\nAll music index tests passed.\n");
		return 0;
	}
	std::printf("\n%d music index test(s) FAILED.\n", g_failures);
	return 1;
}
