#include "pre_inc.h"
#include "kfx/platform/PlatformLinux.h"
#include "kfx/platform/FileFind.h"
#include "platform.h" // kfxmain
#include "bflib_fileio.h"
#include "bflib_datetm.h" // LbDateTime, TbDate, TbTime
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <climits> // PATH_MAX
#include <cerrno>
#include <algorithm>
#include <memory>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <unistd.h>
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

/******************************************************************************/
// Trash: freedesktop.org Trash spec (files/ + info/*.trashinfo under
// $XDG_DATA_HOME/Trash). Writing Path= as the absolute original location is the
// entire point -- it is what lets the desktop's own file manager "Restore" put a
// deleted save straight back into save/ with no restore UI of our own.

// Percent-encode per the Trash spec: everything outside "A-Za-z0-9-_.~/" becomes
// %XX. '/' is left literal -- Path= is a full pathname, not one URI segment.
static std::string trash_percent_encode(const char* path)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char* p = (const unsigned char*)path; *p != '\0'; p++) {
        unsigned char c = *p;
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                    c == '~' || c == '/';
        if (safe) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// mkdir -p. Trash dirs are created 0700: only this user should ever read their
// own trash.
static bool trash_make_dir_recursive(const std::string& dir)
{
    if (dir.empty() || dir == "/")
        return true;
    struct stat sb;
    if (stat(dir.c_str(), &sb) == 0)
        return S_ISDIR(sb.st_mode);
    size_t slash = dir.find_last_of('/');
    if ((slash != std::string::npos) && (slash > 0)) {
        if (!trash_make_dir_recursive(dir.substr(0, slash)))
            return false;
    }
    if ((mkdir(dir.c_str(), 0700) != 0) && (errno != EEXIST))
        return false;
    return true;
}

// $XDG_DATA_HOME/Trash, defaulting to $HOME/.local/share/Trash per the base
// directory spec. Empty when neither variable gives us anywhere to put it.
static std::string trash_home_dir()
{
    const char* xdg = getenv("XDG_DATA_HOME");
    if ((xdg != nullptr) && (xdg[0] != '\0'))
        return std::string(xdg) + "/Trash";
    const char* home = getenv("HOME");
    if ((home == nullptr) || (home[0] == '\0'))
        return std::string();
    return std::string(home) + "/.local/share/Trash";
}

// Canonical absolute path for Path=. realpath() also resolves any "." / ".."
// components, which a relative keeper_runtime_directory can otherwise leave in
// place. Falls back to a plain cwd-join only if realpath() itself fails.
static std::string trash_to_absolute(const char* path)
{
    char resolved[PATH_MAX];
    if (realpath(path, resolved) != nullptr)
        return resolved;
    if (path[0] == '/')
        return path;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr)
        return path;
    return std::string(cwd) + "/" + path;
}

TbBool PlatformLinux::TrashFile(const char* abs_path)
{
    // Under Flatpak, HOME is sandbox-redirected: a "Trash" written there is not the
    // host desktop's trash and no file manager will ever surface it, so writing one
    // would just be a second, invisible fallback trash. Decline immediately and let
    // the caller's own save/trash/ fallback take it instead.
    if (getenv("FLATPAK_ID") != nullptr)
        return false;

    std::string home = trash_home_dir();
    if (home.empty())
        return false;
    std::string files_dir = home + "/files";
    std::string info_dir  = home + "/info";
    if (!trash_make_dir_recursive(files_dir) || !trash_make_dir_recursive(info_dir))
        return false;

    std::string full_path = trash_to_absolute(abs_path);
    const char* slash = strrchr(full_path.c_str(), '/');
    std::string basename = (slash != nullptr) ? (slash + 1) : full_path;
    std::string stem = basename;
    std::string ext;
    size_t dot = basename.find_last_of('.');
    if ((dot != std::string::npos) && (dot > 0)) {
        stem = basename.substr(0, dot);
        ext = basename.substr(dot);
    }

    // Reserve a unique trashinfo name first, atomically (O_EXCL), before touching
    // the save file itself: two deletes racing must not collide on one name, and if
    // anything below fails the original save is still untouched.
    std::string trash_name;
    std::string info_path;
    int info_fd = -1;
    for (int suffix = 0; suffix < 10000; suffix++) {
        trash_name = (suffix == 0) ? basename
            : (stem + "." + std::to_string(suffix + 1) + ext);
        info_path = info_dir + "/" + trash_name + ".trashinfo";
        info_fd = open(info_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (info_fd >= 0)
            break;
        if (errno != EEXIST)
            return false;
    }
    if (info_fd < 0)
        return false; // exhausted the suffix range; give up rather than clobber

    struct TbDate date;
    struct TbTime tm;
    LbDateTime(&date, &tm);
    char info_text[PATH_MAX + 128];
    int len = snprintf(info_text, sizeof(info_text),
        "[Trash Info]\nPath=%s\nDeletionDate=%04u-%02u-%02uT%02u:%02u:%02u\n",
        trash_percent_encode(full_path.c_str()).c_str(),
        (unsigned)date.Year, (unsigned)date.Month, (unsigned)date.Day,
        (unsigned)tm.Hour, (unsigned)tm.Minute, (unsigned)tm.Second);
    bool wrote_ok = (len > 0) && ((size_t)len < sizeof(info_text)) &&
        (write(info_fd, info_text, (size_t)len) == len);
    close(info_fd);
    if (!wrote_ok) {
        unlink(info_path.c_str());
        return false;
    }

    std::string dest = files_dir + "/" + trash_name;
    if (rename(abs_path, dest.c_str()) == 0)
        return true;

    if (errno == EXDEV) {
        // Trash lives on a different filesystem than the save -- rename() can't
        // cross that, so copy the bytes across and remove the original by hand.
        FILE* src = fopen(abs_path, "rb");
        FILE* dst = (src != nullptr) ? fopen(dest.c_str(), "wb") : nullptr;
        bool copy_ok = false;
        if ((src != nullptr) && (dst != nullptr)) {
            char buf[65536];
            size_t n;
            copy_ok = true;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                if (fwrite(buf, 1, n, dst) != n) {
                    copy_ok = false;
                    break;
                }
            }
            copy_ok = copy_ok && (ferror(src) == 0);
        }
        if (src != nullptr) fclose(src);
        if (dst != nullptr) fclose(dst);
        if (copy_ok && (unlink(abs_path) == 0))
            return true;
        unlink(dest.c_str()); // partial copy -- don't leave debris in the trash
    }
    unlink(info_path.c_str());
    return false;
}

bool PlatformLinux::VideoInit()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        return false;
    atexit(SDL_Quit);
    return true;
}

void   PlatformLinux::SetRedbookVolume(SoundVolume) {}
TbBool PlatformLinux::PlayRedbookTrack(int) { return false; }
void   PlatformLinux::PauseRedbookTrack() {}
void   PlatformLinux::ResumeRedbookTrack() {}
void   PlatformLinux::StopRedbookTrack() {}

int  PlatformLinux::InitSteam() { return -1; }
void PlatformLinux::ShutdownSteam() {}

/******************************************************************************/
// Process entry point.

extern "C" int main(int argc, char *argv[])
{
    return kfxmain(argc, argv);
}
