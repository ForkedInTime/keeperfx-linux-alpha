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

	// 5. One unnumbered file alongside a numbered one: the unnumbered file is
	// ignored entirely rather than dragging the whole set into sorted mode.
	// (Previously this forced sorted-position fallback, silently renumbering
	// a correct install when an extra file was dropped into music/ -- the
	// regression that sank upstream PR #5061.)
	{
		std::vector<std::string> in;
		in.push_back("keeper02.ogg"); in.push_back("bonus.flac");
		const std::map<int, std::string> got = build_music_index(in);
		expect_size(got, 1, "unnumbered stray file is ignored, not merged into sorted fallback");
		expect_track(got, 2, "keeper02.ogg", "numbered file keeps its own track number");
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
	{
		std::vector<std::string> in;
		in.push_back("track03.ogg"); in.push_back("keeper03.ogg");
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
	// per dropped file.
	{
		std::vector<std::string> in;
		for (int i = 0; i < 10; ++i) {
			in.push_back(std::string("a") + (char)('0' + i) + ".ogg");
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
	// untitled tracks must now explain all five drops, not just resolve the
	// one number correctly.
	{
		std::vector<std::string> in;
		in.push_back("Ambience.ogg"); in.push_back("Battle Theme.ogg");
		in.push_back("Dungeon Keeper 2.ogg"); in.push_back("Menu.ogg");
		in.push_back("Victory.ogg"); in.push_back("War Drums.ogg");
		std::vector<std::string> notes;
		const std::map<int, std::string> got = build_music_index(in, &notes);
		expect_size(got, 1, "curated folder: only the numbered file survives");
		expect_track(got, 2, "Dungeon Keeper 2.ogg", "curated folder: track 2 resolves correctly");
		if (notes.size() == 5) {
			std::printf("  ok   curated folder: five notes for the five unnumbered files\n");
		} else {
			std::printf("  FAIL curated folder: got %d notes, wanted 5\n", (int)notes.size());
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

	if (g_failures == 0) {
		std::printf("\nAll music index tests passed.\n");
		return 0;
	}
	std::printf("\n%d music index test(s) FAILED.\n", g_failures);
	return 1;
}
