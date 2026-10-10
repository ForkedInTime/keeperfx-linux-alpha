// Integration tests for the Linux directory enumerator in src/linux.cpp.
//
// Unlike test_music_index.cpp -- which exercises pure logic out of a header --
// these run the real LbFileFindFirst/Next/End against a real temporary
// directory, because what they pin down is exactly what a pure function cannot
// see: which entries readdir() hands over and which the enumerator drops.
//
// linux.cpp defines the game's main() and calls two engine functions; the test
// build renames the former with -Dmain=... and stubs the latter below, so no
// engine object is needed. See tests/run.sh.
//
// Build and run:
//   tests/run.sh
#include "../src/bflib_fileio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

// Stubs for the engine symbols the platform layer references. Nothing under
// test reaches any of them: the tests only exercise the file-find path, and
// PlatformManager's window-system accessor is never called by it.
extern "C" void LbErrorParachuteInstall() {}
extern "C" int kfxmain(int, char **) { return 0; }
class WindowSystemSDL;
WindowSystemSDL* GetSDLWindowSystem() { return nullptr; }
// PlatformLinux::GetUserPrefDir() falls back to the game directory; config.c is not linked here.
extern "C" { char keeper_runtime_directory[152] = ""; }

// PlatformLinux::TrashFile timestamps the freedesktop .trashinfo it writes.
// The file-find tests never delete anything, so this only has to resolve.
#include "../src/bflib_datetm.h"
extern "C" TbResult LbDateTime(struct TbDate *, struct TbTime *) { return Lb_SUCCESS; }

static int g_failures = 0;

static void expect(bool ok, const char * what)
{
	if (ok) {
		std::printf("  ok   %s\n", what);
	} else {
		std::printf("  FAIL %s\n", what);
		++g_failures;
	}
}

static bool contains(const std::vector<std::string> & names, const char * name)
{
	for (std::size_t i = 0; i < names.size(); ++i) {
		if (names[i] == name) {
			return true;
		}
	}
	return false;
}

// Everything LbFileFindFirst/Next hand back for the given filespec.
static std::vector<std::string> enumerate(const std::string & filespec)
{
	std::vector<std::string> names;
	struct TbFileEntry fe;
	struct TbFileFind * ff = LbFileFindFirst(filespec.c_str(), &fe);
	if (ff) {
		do {
			names.push_back(fe.Filename);
		} while (LbFileFindNext(ff, &fe) >= 0);
		LbFileFindEnd(ff);
	}
	return names;
}

static int remove_entry(const char * path, const struct stat *, int, struct FTW *)
{
	return std::remove(path);
}

// rm -r, for the nested fixtures.
static void remove_tree(const std::string & path)
{
	nftw(path.c_str(), remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static void touch(const std::string & path)
{
	std::FILE * f = std::fopen(path.c_str(), "wb");
	if (f == NULL) {
		std::printf("  FAIL could not create %s\n", path.c_str());
		++g_failures;
		return;
	}
	std::fputc('x', f);
	std::fclose(f);
}

int main()
{
	char tmpl[] = "/tmp/kfx_file_find_XXXXXX";
	const char * dir = mkdtemp(tmpl);
	if (dir == NULL) {
		std::printf("  FAIL could not create a temporary directory\n");
		return 1;
	}
	const std::string root(dir);

	// A music/ folder as it actually arrives on a Linux install: the real
	// files, a macOS AppleDouble sidecar beside one of them, an editor's
	// hidden dotfile, a subdirectory, and a non-audio file.
	touch(root + "/keeper02.ogg");
	touch(root + "/KEEPER03.OGG");
	touch(root + "/._keeper02.ogg");
	touch(root + "/.hidden.ogg");
	touch(root + "/MusicReadme.txt");
	mkdir((root + "/subdir").c_str(), 0755);
	touch(root + "/subdir/keeper04.ogg");

	{
		const std::vector<std::string> got = enumerate(root + "/*");
		expect(contains(got, "keeper02.ogg"), "a real file is enumerated");
		expect(contains(got, "KEEPER03.OGG"), "an upper-case real file is enumerated");
		expect(contains(got, "MusicReadme.txt"), "a non-audio file is still enumerated (callers filter, not this layer)");
		// The two that matter: both sort ahead of keeper02.ogg because '.'
		// precedes ordinary letters, so anything keying on position gets the
		// sidecar instead of the file.
		expect(!contains(got, "._keeper02.ogg"), "an AppleDouble sidecar is not enumerated");
		expect(!contains(got, ".hidden.ogg"), "a hidden dotfile is not enumerated");
		expect(!contains(got, "subdir"), "a subdirectory is not enumerated");
		expect(!contains(got, "."), "'.' is not enumerated");
		expect(!contains(got, ".."), "'..' is not enumerated");
		expect(got.size() == 3, "exactly the three real files are enumerated");
	}

	{
		// Patterns match case-insensitively, and the filter still applies.
		const std::vector<std::string> got = enumerate(root + "/*.ogg");
		expect(contains(got, "keeper02.ogg"), "*.ogg matches keeper02.ogg");
		expect(contains(got, "KEEPER03.OGG"), "*.ogg matches KEEPER03.OGG case-insensitively");
		expect(!contains(got, "MusicReadme.txt"), "*.ogg does not match a .txt");
		expect(!contains(got, "._keeper02.ogg"), "*.ogg does not match the sidecar");
		expect(got.size() == 2, "exactly the two real oggs match *.ogg");
	}

	{
		// Order is ascending, case-insensitive -- build_music_index() and the
		// campaign list both rely on a stable order across filesystems.
		const std::vector<std::string> got = enumerate(root + "/*.ogg");
		expect(got.size() == 2 && got[0] == "keeper02.ogg" && got[1] == "KEEPER03.OGG",
			"results are sorted case-insensitively");
	}

	{
		// A folder of nothing but hidden entries must look empty, not like a
		// folder with content the caller then fails to load.
		const std::string hidden_only = root + "/hidden_only";
		mkdir(hidden_only.c_str(), 0755);
		touch(hidden_only + "/._only.ogg");
		const std::vector<std::string> got = enumerate(hidden_only + "/*");
		expect(got.empty(), "a folder holding only hidden entries enumerates as empty");
		unlink((hidden_only + "/._only.ogg").c_str());
		rmdir(hidden_only.c_str());
	}

	{
		// A folder named in a config written on Windows, in whatever case:
		// LEVELS_LOCATION = levels/MyPack beside a real levels/mypack. Every
		// file in it opens through LbFileOpen's case-insensitive fallback, so
		// the listing has to find the folder the same way -- before, it found
		// nothing and the map pack was rejected for having no .LIF files.
		mkdir((root + "/levels").c_str(), 0755);
		mkdir((root + "/levels/mypack").c_str(), 0755);
		touch(root + "/levels/mypack/map00001.lif");
		touch(root + "/levels/mypack/MAP00002.LIF");
		touch(root + "/levels/mypack/readme.txt");
		std::vector<std::string> got = enumerate(root + "/levels/MyPack/*.lif");
		expect(got.size() == 2 && got[0] == "map00001.lif" && got[1] == "MAP00002.LIF",
			"a directory whose case differs from disk is still listed");
		got = enumerate(root + "/LEVELS/MYPACK/*.lif");
		expect(got.size() == 2, "every directory segment is matched case-insensitively");
		got = enumerate(root + "/levels/mypack/*.lif");
		expect(got.size() == 2, "an exactly-spelt directory is listed as before");
		got = enumerate(root + "/levels/OtherPack/*.lif");
		expect(got.empty(), "a directory that does not exist in any case lists nothing");
	}

	{
		// Only the file-name part is a pattern; the directory is a literal
		// name. fnmatch() used to run over the whole path, so the brackets in
		// an INSTALL_PATH such as "/games/DK [GOG]" were a character class
		// that never matches the literal "[GOG]" -- every listing under such
		// an install came back empty.
		const std::string gog = root + "/DK [GOG]";
		mkdir(gog.c_str(), 0755);
		touch(gog + "/campaign.cfg");
		touch(gog + "/other.txt");
		std::vector<std::string> got = enumerate(gog + "/*.cfg");
		expect(got.size() == 1 && got[0] == "campaign.cfg", "a directory named \"DK [GOG]\" is listed");
		got = enumerate(root + "/dk [gog]/*.cfg");
		expect(got.size() == 1, "a bracketed directory name also matches case-insensitively");

		// '?' and '*' in a folder name are literal too.
		const std::string odd = root + "/What? Keeper*";
		mkdir(odd.c_str(), 0755);
		touch(odd + "/odd.cfg");
		got = enumerate(odd + "/*.cfg");
		expect(got.size() == 1 && got[0] == "odd.cfg", "a directory named with '?' and '*' is listed");
	}

	{
		// '\' written on Windows as a separator: LEVELS_LOCATION = levels\MyPack,
		// a creature sound "creature\maiden\HIT1.wav". Read as one file name,
		// neither ever existed on Linux.
		std::vector<std::string> got = enumerate(root + "/levels\\MyPack/*.lif");
		expect(got.size() == 2, "a directory written with '\\' separators is listed");
		mkdir((root + "/creature").c_str(), 0755);
		mkdir((root + "/creature/maiden").c_str(), 0755);
		touch(root + "/creature/maiden/hit1.wav");
		const std::string win_path = root + "/creature\\maiden\\HIT1.wav";
		char buf[4096];
		expect(LbFileExists(win_path.c_str()) == 1, "LbFileExists reads '\\' as a separator");
		expect(std::string(LbFileCaseInsensitivePath(win_path.c_str(), buf, sizeof(buf))) == root + "/creature/maiden/hit1.wav",
			"LbFileCaseInsensitivePath gives the real path for a '\\'-separated name");
		TbFileHandle fh = LbFileOpen(win_path.c_str(), Lb_FILE_MODE_READ_ONLY);
		expect(fh != NULL, "LbFileOpen opens a '\\'-separated name");
		if (fh != NULL) {
			LbFileClose(fh);
		}

		// A Linux file name may contain a literal '\'. It is still found as
		// it is spelt -- also case-insensitively -- and never re-read as a path.
		touch(root + "/odd\\name.cfg");
		const std::string literal = root + "/odd\\name.cfg";
		expect(std::string(LbFileCaseInsensitivePath(literal.c_str(), buf, sizeof(buf))) == literal,
			"a file name containing a literal '\\' resolves to itself");
		expect(std::string(LbFileCaseInsensitivePath((root + "/ODD\\NAME.CFG").c_str(), buf, sizeof(buf))) == literal,
			"a literal '\\' name still matches case-insensitively");
		got = enumerate(root + "/*.cfg");
		expect(got.size() == 1 && got[0] == "odd\\name.cfg", "a literal '\\' name is enumerated");
		unlink(literal.c_str());
		remove_tree(root + "/creature");
	}

	remove_tree(root + "/levels");
	remove_tree(root + "/DK [GOG]");
	remove_tree(root + "/What? Keeper*");

	unlink((root + "/keeper02.ogg").c_str());
	unlink((root + "/KEEPER03.OGG").c_str());
	unlink((root + "/._keeper02.ogg").c_str());
	unlink((root + "/.hidden.ogg").c_str());
	unlink((root + "/MusicReadme.txt").c_str());
	unlink((root + "/subdir/keeper04.ogg").c_str());
	rmdir((root + "/subdir").c_str());
	rmdir(root.c_str());

	if (g_failures > 0) {
		std::printf("\n%d file find test(s) FAILED.\n", g_failures);
		return 1;
	}
	std::printf("\nAll file find tests passed.\n");
	return 0;
}
