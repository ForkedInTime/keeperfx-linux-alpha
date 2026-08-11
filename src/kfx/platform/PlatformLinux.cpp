#include "pre_inc.h"
#include "kfx/platform/PlatformLinux.h"
#include "kfx/platform/FileFind.h"
#include "bflib_fileio.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <memory>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include "post_inc.h"

static bool filespec_is_pattern(const char* filespec)
{
    return strchr(filespec, '*') != nullptr;
}

static std::string directory_from_filespec(const char* filespec)
{
    const auto sep = strrchr(filespec, '/');
    if (sep && sep != filespec) {
        return std::string(filespec, sep - filespec);
    }
    return ".";
}

const char* PlatformLinux::GetOSVersion() const { return "Linux"; }
const void* PlatformLinux::GetImageBase() const { return nullptr; }
const char* PlatformLinux::GetWineVersion() const { return nullptr; } // running native
const char* PlatformLinux::GetWineHost() const { return nullptr; }    // running native

TbFileFind* PlatformLinux::FileFindFirst(const char* filespec, TbFileEntry* entry)
{
    try {
        auto ff = std::make_unique<TbFileFind>();
        bool is_pattern = filespec_is_pattern(filespec);
        std::string path = is_pattern ? directory_from_filespec(filespec) : filespec;
        DIR* handle = opendir(path.c_str());
        if (handle) {
            while (auto de = readdir(handle)) {
                // Hidden entries are not the user's content on Unix, and
                // readdir() hands them over because the fnmatch() call below
                // omits FNM_PERIOD: a filespec of "*" or "*.cfg" matches
                // "._campaign.cfg" as readily as the real file. That matters
                // because campaigns, mods and workshop content reach a Linux
                // install as archives built on macOS or Windows, which
                // routinely carry AppleDouble sidecars ("._x.cfg", "._x.zip")
                // beside every real file. Enumerated, they get parsed as
                // campaigns, fed to the sprite loader as zips, counted as
                // levels, and reported on-screen as a mod to install -- and
                // since '.' sorts before ordinary letters, they land ahead of
                // the file they shadow, so any caller keying on position picks
                // the sidecar. build_music_index() already filters them for
                // music/; doing it here covers every caller instead.
                //
                // This subsumes the "." and ".." skips it replaces. Carried
                // across when upstream #5107 moved enumeration into the
                // platform layer; their version skips only "." and "..".
                if (de->d_name[0] == '.') {
                    continue;
                }
                const std::string file_path = path + "/" + de->d_name;
                if (is_pattern && fnmatch(filespec, file_path.c_str(), FNM_FILE_NAME | FNM_CASEFOLD) != 0) {
                    continue;
                }
                struct stat sb;
                if (stat(file_path.c_str(), &sb) < 0 || !S_ISREG(sb.st_mode)) {
                    continue;
                }
                std::string key = de->d_name;
                for (size_t i = 0; i < key.size(); i++) {
                    key[i] = (char)tolower((unsigned char)key[i]);
                }
                ff->names.emplace_back(key, de->d_name);
            }
            closedir(handle);
        }
        if (!ff->names.empty()) {
            std::sort(ff->names.begin(), ff->names.end());
            entry->Filename = ff->names[0].second.c_str();
            return ff.release();
        }
    } catch (...) {}
    return nullptr;
}

bool PlatformLinux::VideoInit()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        return false;
    atexit(SDL_Quit);
    return true;
}
