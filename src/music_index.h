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
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
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

// fname with its recognised extension removed, given the rank already
// established for it by music_extension_rank() (so this never re-derives or
// second-guesses that match). Used only to detect "same song, different
// format" among files in sorted-fallback mode.
inline std::string music_stem(const std::string & fname, int rank) {
	const std::size_t ext_len = std::strlen(MUSIC_EXTENSIONS[rank]);
	return fname.substr(0, fname.size() - ext_len);
}

// Last run of digits in the basename, extension stripped, parsed as a
// non-negative integer. -1 when there is no trailing digit run, or when the
// run does not fit back into the int this function returns.
//
// This only guards against overflow/UB in the parse itself (formerly atoi(),
// which let e.g. "track4294967298.ogg" silently wrap around and parse as
// track 2). It deliberately does NOT check the number against
// MUSIC_TRACK_MIN/MUSIC_TRACK_MAX: a small out-of-range number such as 0 or 1
// still comes back as itself, because build_music_index() needs to tell "no
// number" (ignored) apart from "a real but out-of-range number" (forces the
// sorted-position fallback) — folding the track-range check in here would
// make those indistinguishable.
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
	const std::string digits = stem.substr(begin, end - begin);
	errno = 0;
	const long value = strtol(digits.c_str(), nullptr, 10);
	if (errno == ERANGE || value > INT_MAX) {
		// Overflows long outright, or parses fine but does not fit back into
		// the int this function returns: either way, not a plausible track
		// number, so treat it the same as "no number" rather than let the
		// narrowing conversion silently wrap it into a small bogus one.
		return -1;
	}
	return (int)value;
}

// Maps track number -> filename for the given directory contents. Entries are
// bare filenames, not paths; anything that is not a recognised audio file is
// ignored.
//
// When notes is non-null, one short human-readable line is appended for each
// recognised audio file dropped from the result (a same/lower-priority-format
// collision loser, a file sorted-mode had no track left for, or -- in numeric
// mode -- a file with no track number in its name) so a caller can surface
// why a file the user has in music/ is not actually playing. Hidden/dotfile
// entries are not "the user's music" and are filtered out silently, with no
// note, before any of this.
inline std::map<int, std::string> build_music_index(const std::vector<std::string> & entries,
	std::vector<std::string> * notes = nullptr) {
	// Keep the audio files, remembering each one's format preference.
	std::vector<std::pair<std::string, int> > files;
	for (std::size_t i = 0; i < entries.size(); ++i) {
		// Hidden files are not content on Unix. This also covers macOS
		// AppleDouble sidecars (e.g. "._keeper02.ogg"): without this check
		// they would beat their real counterpart in every collision, because
		// '.' sorts before ordinary letters, and macOS-created zips/exFAT
		// transfers leave them sitting right next to the real file.
		if (!entries[i].empty() && entries[i][0] == '.') {
			continue;
		}
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

	// Numeric mode needs at least one file to carry a track number, AND every
	// file that carries one to be a track the game asks for. Files that carry
	// no number at all take no part in that decision and are simply left out
	// of the index in numeric mode: a stray untitled track dropped alongside
	// a complete numbered set must not renumber (or discard) everything else
	// — that renumbering was the regression that sank upstream PR #5061. A
	// file whose number falls outside the game's track range is different:
	// the filenames can no longer describe a complete numbered set, so that
	// still forces the sorted-position fallback, exactly as before.
	bool any_numbered = false;
	bool numeric = true;
	for (std::size_t i = 0; numeric && i < files.size(); ++i) {
		const int track = music_trailing_number(files[i].first);
		if (track < 0) {
			continue;
		}
		any_numbered = true;
		if (track < MUSIC_TRACK_MIN || track > MUSIC_TRACK_MAX) {
			numeric = false;
		}
	}
	numeric = numeric && any_numbered;

	std::map<int, std::string> index;
	if (numeric) {
		std::map<int, int> best_rank;
		for (std::size_t i = 0; i < files.size(); ++i) {
			const int track = music_trailing_number(files[i].first);
			if (track < 0) {
				// Ignored entirely in numeric mode. Record why: the file is
				// otherwise valid music, so a silent drop here is exactly the
				// undiagnosable-silence bug this whole feature exists to fix.
				if (notes) {
					notes->push_back("dropped " + files[i].first +
						": no track number found in the filename; rename it to include one "
						"(2-7, e.g. keeper05.ogg) so it can be assigned a track");
				}
				continue; // no number: ignored entirely in numeric mode
			}
			const std::map<int, int>::iterator seen = best_rank.find(track);
			// Strictly-better keeps the first of an equal-ranked pair, so a
			// same-format clash resolves to whichever sorts first.
			if (seen == best_rank.end()) {
				index[track] = files[i].first;
				best_rank[track] = files[i].second;
			} else if (files[i].second < seen->second) {
				if (notes) {
					notes->push_back("dropped " + index[track] + " for track " + std::to_string(track) +
						": " + files[i].first + " is a higher-preference format");
				}
				index[track] = files[i].first;
				best_rank[track] = files[i].second;
			} else if (notes) {
				notes->push_back("dropped " + files[i].first + " for track " + std::to_string(track) +
					": " + index[track] + " already claims it in an equal-or-better format");
			}
		}
	} else {
		// Sorted fallback keys entirely on position, so leaving the raw file
		// list untouched would let two files that are really the same song in
		// different formats (a FLAC re-rip left alongside the original OGGs)
		// land on two different tracks: doubling up one song and, because
		// sorted mode fills every track it can, silently pushing a genuinely
		// distinct song out of range. Deduplicate by filename stem
		// (case-insensitive) first, keeping each stem's best-format
		// representative under the same FLAC > WAV > OGG > MP3 preference
		// numeric mode already applies per track number; an equal-rank tie
		// (same extension, different case) keeps whichever sorts first,
		// exactly as numeric mode's same-format tiebreak.
		std::map<std::string, std::size_t> best_for_stem; // lower stem -> index into files
		for (std::size_t i = 0; i < files.size(); ++i) {
			const std::string lower_stem = music_to_lower(music_stem(files[i].first, files[i].second));
			const std::map<std::string, std::size_t>::iterator seen = best_for_stem.find(lower_stem);
			if (seen == best_for_stem.end()) {
				best_for_stem[lower_stem] = i;
			} else if (files[i].second < files[seen->second].second) {
				if (notes) {
					notes->push_back("dropped " + files[seen->second].first + ": " + files[i].first +
						" is a higher-preference format of the same track");
				}
				seen->second = i;
			} else if (notes) {
				notes->push_back("dropped " + files[i].first + ": " + files[seen->second].first +
					" is a higher-preference format of the same track");
			}
		}

		// Surviving representatives, sorted by stem (case-insensitive, full
		// filename as tiebreak) rather than by full filename: this keeps the
		// track ordering stable no matter which format happened to win each
		// stem group above.
		std::vector<std::size_t> reps;
		for (std::map<std::string, std::size_t>::const_iterator it = best_for_stem.begin();
			it != best_for_stem.end(); ++it) {
			reps.push_back(it->second);
		}
		std::sort(reps.begin(), reps.end(),
			[&files](std::size_t a, std::size_t b) {
				const std::string sa = music_to_lower(music_stem(files[a].first, files[a].second));
				const std::string sb = music_to_lower(music_stem(files[b].first, files[b].second));
				return (sa != sb) ? (sa < sb) : (files[a].first < files[b].first);
			});

		int track = MUSIC_TRACK_MIN;
		std::size_t i = 0;
		for (; i < reps.size() && track <= MUSIC_TRACK_MAX; ++i) {
			index[track] = files[reps[i]].first;
			++track;
		}
		if (notes) {
			for (; i < reps.size(); ++i) {
				notes->push_back("dropped " + files[reps[i]].first + ": sorted mode already filled tracks " +
					std::to_string(MUSIC_TRACK_MIN) + "-" + std::to_string(MUSIC_TRACK_MAX));
			}
		}
	}
	return index;
}
