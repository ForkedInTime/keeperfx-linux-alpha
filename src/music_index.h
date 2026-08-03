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
