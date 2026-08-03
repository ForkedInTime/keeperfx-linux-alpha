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
