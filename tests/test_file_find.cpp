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
#include <sys/stat.h>
#include <unistd.h>

// Stubs for the engine symbols the platform layer references. Nothing under
// test reaches any of them: the tests only exercise the file-find path, and
// PlatformManager's window-system accessor is never called by it.
extern "C" void LbErrorParachuteInstall() {}
extern "C" int kfxmain(int, char **) { return 0; }
class WindowSystemSDL;
WindowSystemSDL* GetSDLWindowSystem() { return nullptr; }

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
