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
// lossless first, then the better lossy codec. This order is load-bearing
// for the index's collision rule (build_music_index() below) and must not
// change for that reason.
static const char * const MUSIC_EXTENSIONS[] = { ".flac", ".wav", ".ogg", ".mp3" };
static const int MUSIC_EXTENSION_COUNT = 4;

// Same four extensions, but in the order play_music_track()'s direct
// stock-name lookup (keeper%02d.<ext>) probes them in. This is deliberately
// NOT the same order as MUSIC_EXTENSIONS: the direct lookup existed before
// this feature and always checked OGG first (the format the original stock
// install shipped), so a keeper02.ogg sitting next to a keeper02.flac must
// keep playing the OGG -- exactly what an upgrading install already played
// before this feature existed. MUSIC_EXTENSIONS' FLAC-first order is only a
// preference for the *index*, which never had pre-existing behaviour to
// preserve, so it is free to prefer lossless. Do not merge these two arrays.
static const char * const MUSIC_DIRECT_LOOKUP_EXTENSIONS[] = { ".ogg", ".flac", ".wav", ".mp3" };
static const int MUSIC_DIRECT_LOOKUP_EXTENSION_COUNT = 4;

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

// fname with every trailing recognised extension removed, repeatedly. A
// name has only one *real* extension, but a file like "keeper02.mp3.ogg" (a
// renamed/re-exported file that kept its old extension as part of the name)
// or "a.ogg.flac" has a second recognised-looking one immediately before
// it. Stopping after a single strip -- as music_stem() deliberately does,
// since it only ever removes the one extension its caller already resolved
// -- leaves that embedded token in the stem, which corrupts two things that
// scan a stem from the right: music_trailing_number() below (the "3" in
// ".mp3" gets mistaken for the track number instead of the "02") and
// sorted-fallback dedup's stem comparison ("a.ogg" and "a.ogg.flac" would
// count as different songs instead of the same one). Ordinary single-
// extension names only ever loop once here.
inline std::string music_strip_extensions(const std::string & fname) {
	std::string stem = fname;
	for (;;) {
		const int rank = music_extension_rank(stem);
		if (rank < 0) {
			break;
		}
		stem = music_stem(stem, rank);
	}
	return stem;
}

// Last run of digits in the basename, every trailing recognised extension
// stripped (see music_strip_extensions() -- this is what keeps
// "keeper02.mp3.ogg" reading as track 2 rather than the "3" embedded in
// ".mp3"), parsed as a non-negative integer. -1 when there is no trailing
// digit run, or when the run does not fit back into the int this function
// returns.
//
// When had_digit_run is non-null, it is set to whether a trailing digit run
// was found at all, distinguishing the two different reasons for a -1
// return: "this file has no number in its name" vs. "it has a number, but
// one too large to be a plausible track". build_music_index() uses this to
// word its notes accurately (see FIX 5) instead of telling a file with an
// absurdly large number that it has "no track number" when it has one.
//
// The overflow guard only protects the parse itself (formerly atoi(), which
// let e.g. "track4294967298.ogg" silently wrap around and parse as track 2).
// It deliberately does NOT check the number against
// MUSIC_TRACK_MIN/MUSIC_TRACK_MAX: a small out-of-range number such as 0 or 1
// still comes back as itself, because build_music_index() needs to tell "no
// number" (ignored) apart from "a real but out-of-range number" — folding
// the track-range check in here would make those indistinguishable.
inline int music_trailing_number(const std::string & fname, bool * had_digit_run = nullptr) {
	const std::string stem = music_strip_extensions(fname);
	std::string::size_type end = std::string::npos;
	for (std::string::size_type i = stem.size(); i-- > 0; ) {
		if (isdigit((unsigned char)stem[i])) {
			end = i + 1;
			break;
		}
	}
	if (end == std::string::npos) {
		if (had_digit_run) {
			*had_digit_run = false;
		}
		return -1;
	}
	if (had_digit_run) {
		*had_digit_run = true;
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
// recognised audio file this function itself chose not to use (a same/lower-
// priority-format collision loser, a file sorted-mode had no track left for,
// a file numeric mode had no number -- or no in-range number -- for). Each
// note describes *this function's own mapping decision*, not what the game
// actually plays: build_music_index() is deliberately kept unaware of
// play_music_track()'s separate direct stock-name lookup (see
// docs/design/specs/2026-08-03-music-track-detection-design.md), so a
// file this function "dropped" may still be exactly what plays, via that
// other path, and a file it kept may be shadowed by it. Callers that want to
// describe actual playback must account for the direct lookup themselves.
// Hidden/dotfile entries are not "the user's music" and are filtered out
// silently, with no note, before any of this.
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

	// Build both candidate mappings, then use whichever actually resolves
	// more tracks -- numeric wins ties. This replaces an earlier all-or-
	// nothing rule ("numeric mode only if every numbered file is in range,
	// otherwise sorted fallback for everything") that let a single
	// out-of-range-numbered file force the whole folder into sorted
	// position-based mode, which renumbers and can truncate a working,
	// otherwise-correctly-numbered set. Building both and comparing resolved
	// counts means an incidental stray file can never make things worse than
	// leaving it out would have: sorted only wins by actually placing more
	// files into the 2-7 range than numeric could.
	std::vector<std::string> numeric_notes;
	std::map<int, std::string> numeric_index;
	{
		std::map<int, int> best_rank;
		for (std::size_t i = 0; i < files.size(); ++i) {
			bool had_digit_run = false;
			const int track = music_trailing_number(files[i].first, &had_digit_run);
			if (track < 0) {
				// Not used by the numeric candidate at all. Record why: the
				// file is otherwise valid music, so a silent drop here is
				// exactly the undiagnosable-silence bug this feature exists
				// to fix. had_digit_run tells "no number in the name" apart
				// from "a number too large to be plausible" (FIX 5) -- both
				// used to be reported as "no track number found", which is
				// false for the second case.
				if (notes) {
					numeric_notes.push_back(had_digit_run
						? ("not used by the music index: " + files[i].first +
							"'s number is too large to be a track; rename it with a number in " +
							std::to_string(MUSIC_TRACK_MIN) + "-" + std::to_string(MUSIC_TRACK_MAX) +
							" (e.g. keeper05.ogg)")
						: ("not used by the music index: " + files[i].first +
							" has no track number in its filename; rename it to include one (" +
							std::to_string(MUSIC_TRACK_MIN) + "-" + std::to_string(MUSIC_TRACK_MAX) +
							", e.g. keeper05.ogg) so it can be assigned a track"));
				}
				continue;
			}
			if (track < MUSIC_TRACK_MIN || track > MUSIC_TRACK_MAX) {
				// A real number, just not one the game asks for. Distinct
				// from "no number" above -- this file could be renamed to
				// join the numbered set, not merely have a number added.
				if (notes) {
					numeric_notes.push_back("not used by the music index: " + files[i].first +
						"'s track number (" + std::to_string(track) + ") is outside the playable range " +
						std::to_string(MUSIC_TRACK_MIN) + "-" + std::to_string(MUSIC_TRACK_MAX));
				}
				continue;
			}
			const std::map<int, int>::iterator seen = best_rank.find(track);
			// Strictly-better keeps the first of an equal-ranked pair, so a
			// same-format clash resolves to whichever sorts first.
			if (seen == best_rank.end()) {
				numeric_index[track] = files[i].first;
				best_rank[track] = files[i].second;
			} else if (files[i].second < seen->second) {
				if (notes) {
					numeric_notes.push_back("not used by the music index for track " + std::to_string(track) +
						": " + numeric_index[track] + " was superseded by " + files[i].first +
						", a higher-preference format");
				}
				numeric_index[track] = files[i].first;
				best_rank[track] = files[i].second;
			} else if (notes) {
				numeric_notes.push_back("not used by the music index for track " + std::to_string(track) +
					": " + files[i].first + " -- " + numeric_index[track] +
					" already claims it in an equal-or-better format");
			}
		}
	}

	// Sorted fallback keys entirely on position, so leaving the raw file
	// list untouched would let two files that are really the same song in
	// different formats (a FLAC re-rip left alongside the original OGGs)
	// land on two different tracks: doubling up one song and, because
	// sorted mode fills every track it can, silently pushing a genuinely
	// distinct song out of range. Deduplicate by filename stem
	// (case-insensitive, every trailing recognised extension stripped --
	// see music_strip_extensions() -- so "a.ogg" and "a.ogg.flac" count as
	// the same song too) first, keeping each stem's best-format
	// representative under the same FLAC > WAV > OGG > MP3 preference
	// numeric mode already applies per track number; an equal-rank tie
	// (same extension, different case) keeps whichever sorts first, exactly
	// as numeric mode's same-format tiebreak.
	std::vector<std::string> sorted_notes;
	std::map<int, std::string> sorted_index;
	{
		std::map<std::string, std::size_t> best_for_stem; // lower stripped stem -> index into files
		for (std::size_t i = 0; i < files.size(); ++i) {
			const std::string lower_stem = music_to_lower(music_strip_extensions(files[i].first));
			const std::map<std::string, std::size_t>::iterator seen = best_for_stem.find(lower_stem);
			if (seen == best_for_stem.end()) {
				best_for_stem[lower_stem] = i;
			} else if (files[i].second < files[seen->second].second) {
				if (notes) {
					sorted_notes.push_back("not used by the music index: " + files[seen->second].first +
						"; " + files[i].first + " is a higher-preference format of the same song");
				}
				seen->second = i;
			} else if (notes) {
				if (files[i].second == files[seen->second].second) {
					// Equal rank (e.g. "Song.OGG" vs "song.ogg"): the two
					// formats are identical, so it would be false to call
					// either one "higher-preference" -- the real reason the
					// survivor won is sort order.
					sorted_notes.push_back("not used by the music index: " + files[i].first + "; " +
						files[seen->second].first + " is the same format and sorts first");
				} else {
					sorted_notes.push_back("not used by the music index: " + files[i].first + "; " +
						files[seen->second].first + " is a higher-preference format of the same song");
				}
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
				const std::string sa = music_to_lower(music_strip_extensions(files[a].first));
				const std::string sb = music_to_lower(music_strip_extensions(files[b].first));
				return (sa != sb) ? (sa < sb) : (files[a].first < files[b].first);
			});

		int track = MUSIC_TRACK_MIN;
		std::size_t i = 0;
		for (; i < reps.size() && track <= MUSIC_TRACK_MAX; ++i) {
			sorted_index[track] = files[reps[i]].first;
			++track;
		}
		if (notes) {
			for (; i < reps.size(); ++i) {
				sorted_notes.push_back("not used by the music index: " + files[reps[i]].first +
					"; sorted mode already filled tracks " + std::to_string(MUSIC_TRACK_MIN) + "-" +
					std::to_string(MUSIC_TRACK_MAX));
			}
		}
	}

	// The tiebreak: sorted only wins by strictly resolving more tracks than
	// numeric managed. Equal counts favour numeric, since a numeric mapping
	// reflects filenames the user (or a campaign) chose deliberately, while
	// sorted position is incidental.
	const bool use_sorted = sorted_index.size() > numeric_index.size();
	if (notes) {
		const std::vector<std::string> & chosen_notes = use_sorted ? sorted_notes : numeric_notes;
		notes->insert(notes->end(), chosen_notes.begin(), chosen_notes.end());
	}
	return use_sorted ? sorted_index : numeric_index;
}
