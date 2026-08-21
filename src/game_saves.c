/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_saves.c
 *     Saved games maintain functions.
 * @par Purpose:
 *     For opening, writing, listing saved games.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     27 Jan 2009 - 25 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_saves.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_datetm.h"

#include "config.h"
#include "config_keeperfx.h"
#include "config_campaigns.h"
#include "config_strings.h"
#include "config_translation.h"
#include "kfx/platform/PlatformManager.h"
#include "dungeon_stats.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_compp.h"
#include "sound_manager.h"
#include "custom_sprites.h"
#include "front_simple.h"
#include "frontend.h"
#include "frontmenu_ingame_tabs.h"
#include "front_landview.h"
#include "front_highscore.h"
#include "front_lvlstats.h"
#include "lens_api.h"
#include "local_camera.h"
#include "gui_soundmsgs.h"
#include "game_legacy.h"
#include "game_merge.h"
#include "frontmenu_ingame_map.h"
#include "gui_boxmenu.h"
#include "net_exchange_gameplay.h"
#include "packets.h"
#include "keeperfx.hpp"
#include "api.h"
#include "lvl_filesdk1.h"
#include "lua_base.h"
#include "lua_triggers.h"
#include "moonphase.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
TbBool load_catalogue_entry(TbFileHandle fh,struct FileChunkHeader *hdr,struct CatalogueEntry *centry);
/******************************************************************************/
const short VersionMajor    = VER_MAJOR;
const short VersionMinor    = VER_MINOR;
short const VersionRelease  = VER_RELEASE;
short const VersionBuild    = VER_BUILD;

const char *continue_game_filename="fx1contn.sav";
const char *saved_game_filename="fx1g%04d.sav";
const char *packet_filename="fx1rp%04d.pck";

/* Dynamically-grown savegame catalogue (see game_saves.h): holds one CatalogueEntry per
 * reachable save slot. It is sized on load to (highest existing slot + 2), min
 * SAVE_SLOTS_MIN, and grown on demand when writing to a higher slot, so the slot count
 * is limited only by disk space. */
struct CatalogueEntry *save_game_catalogue = NULL;
long save_game_catalogue_count = 0;      // logical length (slots the menus range over)
static long save_game_catalogue_capacity = 0;  // number of entries actually allocated

int number_of_saved_games;

/** Grow the catalogue so that index slot is valid; any newly added entries are zeroed
 *  (not in use). Returns false only on allocation failure. */
static TbBool ensure_catalogue_slot(long slot)
{
    if ((slot < 0) || (slot >= SAVE_SLOTS_LIMIT))
        return false;
    if (slot >= save_game_catalogue_capacity)
    {
        long newcap = slot + 1;
        struct CatalogueEntry* p = (struct CatalogueEntry*)realloc(save_game_catalogue,
            newcap * sizeof(struct CatalogueEntry));
        if (p == NULL)
        {
            ERRORLOG("Cannot grow save catalogue to %ld entries", newcap);
            return false;
        }
        memset(&p[save_game_catalogue_capacity], 0,
            (newcap - save_game_catalogue_capacity) * sizeof(struct CatalogueEntry));
        save_game_catalogue = p;
        save_game_catalogue_capacity = newcap;
    }
    if (slot >= save_game_catalogue_count)
        save_game_catalogue_count = slot + 1;
    return true;
}

/** Parse the slot index from a savegame filename "fx1gNNNN.sav" (case-insensitive).
 *  Returns the index, or -1 if the name does not match the expected pattern. */
static int save_slot_index_from_filename(const char *fname)
{
    if (strncasecmp(fname, "fx1g", 4) != 0)
        return -1;
    const char* p = fname + 4;
    if ((*p < '0') || (*p > '9'))
        return -1;
    long idx = atol(p);
    while ((*p >= '0') && (*p <= '9'))
        p++;
    if (strcasecmp(p, ".sav") != 0)
        return -1;
    if ((idx < 0) || (idx >= SAVE_SLOTS_LIMIT))
        return -1;
    return (int)idx;
}

#define CONTINUE_GAME_FILE_SIZE (CAMPAIGN_FNAME_LEN + sizeof(LevelNumber) + sizeof(struct IntralevelData))
/******************************************************************************/
TbBool is_primitive_save_version(long filesize)
{
    if (filesize < (char *)&game.loaded_level_number - (char *)&game)
        return false;
    if (filesize <= 1382437) // sizeof(struct Game) - but it's better to use constant here
        return true;
    return false;
}

TbBool save_game_chunks(TbFileHandle fhandle, struct CatalogueEntry *centry)
{
    struct FileChunkHeader hdr;
    long chunks_done = 0;
    // Currently there is some game data outside of structs - make sure it is updated
    light_export_system_state(&game.lightst);
    { // Info chunk
        hdr.id = SGC_InfoBlock;
        hdr.ver = 0;
        hdr.len = sizeof(struct CatalogueEntry);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, centry, sizeof(struct CatalogueEntry)) == sizeof(struct CatalogueEntry))
            chunks_done |= SGF_InfoBlock;
    }
    { // Game data chunk
        hdr.id = SGC_GameOrig;
        hdr.ver = 0;
        hdr.len = sizeof(struct Game);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &game, sizeof(struct Game)) == sizeof(struct Game))
            chunks_done |= SGF_GameOrig;
    }
    { // IntralevelData data chunk
        hdr.id = SGC_IntralevelData;
        hdr.ver = 0;
        hdr.len = sizeof(struct IntralevelData);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &intralvl, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData))
            chunks_done |= SGF_IntralevelData;
    }

    // Adding Lua serialized data chunk
    {
        size_t lua_data_len;
        const char* lua_data = lua_get_serialised_data(&lua_data_len);

        hdr.id = SGC_LuaData;
        hdr.ver = 0;
        hdr.len = lua_data_len;
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, lua_data, lua_data_len) == lua_data_len)
            chunks_done |= SGF_LuaData;
        cleanup_serialized_data();
    }

    if (chunks_done != SGF_SavedGame)
        return false;
    return true;
}

TbBool save_packet_chunks(TbFileHandle fhandle,struct CatalogueEntry *centry)
{
    struct FileChunkHeader hdr;
    long chunks_done = 0;
    { // Packet file header
        hdr.id = SGC_PacketHeader;
        hdr.ver = 0;
        hdr.len = sizeof(struct PacketSaveHead);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, &game.packet_save_head, sizeof(struct PacketSaveHead)) == sizeof(struct PacketSaveHead))
            chunks_done |= SGF_PacketHeader;
    }
    { // Info chunk
        hdr.id = SGC_InfoBlock;
        hdr.ver = 0;
        hdr.len = sizeof(struct CatalogueEntry);
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        if (LbFileWrite(fhandle, centry, sizeof(struct CatalogueEntry)) == sizeof(struct CatalogueEntry))
            chunks_done |= SGF_InfoBlock;
    }
    // If it's not start of a level, save progress data too
    if (get_gameturn() != 0)
    {
        { // Game data chunk
            hdr.id = SGC_GameOrig;
            hdr.ver = 0;
            hdr.len = sizeof(struct Game);
            if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            if (LbFileWrite(fhandle, &game, sizeof(struct Game)) == sizeof(struct Game))
                chunks_done |= SGF_GameOrig;
        }
    }
    { // Packet file data start indicator
        hdr.id = SGC_PacketData;
        hdr.ver = 0;
        hdr.len = 0;
        if (LbFileWrite(fhandle, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
            chunks_done |= SGF_PacketData;
    }
    if ((chunks_done != SGF_PacketStart) && (chunks_done != SGF_PacketContinue))
        return false;
    return true;
}

int load_game_chunks(TbFileHandle fhandle, struct CatalogueEntry *centry)
{
    long chunks_done = 0;
    while (!LbFileEof(fhandle))
    {
        struct FileChunkHeader hdr;
        if (LbFileRead(fhandle, &hdr, sizeof(struct FileChunkHeader)) != sizeof(struct FileChunkHeader))
            break;
        switch (hdr.id)
        {
        case SGC_InfoBlock:
            if (load_catalogue_entry(fhandle, &hdr, centry))
            {
                chunks_done |= SGF_InfoBlock;
                if (!change_campaign(CampgnT_Default, centry->campaign_fname)) {
                    ERRORLOG("Unable to load campaign");
                    return GLoad_Failed;
                }
                free_level_strings_data();
                struct GameCampaign *campgn = &campaign;
                load_map_string_data(campgn, centry->level_num, get_level_fgroup(centry->level_num));
                // Load configs which may have per-campaign part, and even be modified within a level
                recheck_all_mod_exist();
                init_custom_sprites(centry->level_num);
                load_stats_files();
                snprintf(high_score_entry, PLAYER_NAME_LENGTH, "%s", centry->player_name);
            }
            break;
        case SGC_GameOrig:
            if (hdr.len != sizeof(struct Game))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible GameOrig chunk");
                break;
            }
            if (LbFileRead(fhandle, &game, sizeof(struct Game)) == sizeof(struct Game)) {
                chunks_done |= SGF_GameOrig;
            } else {
                WARNLOG("Could not read GameOrig chunk");
            }
            break;
        case SGC_PacketHeader:
            if (hdr.len != sizeof(struct PacketSaveHead))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible PacketHeader chunk");
                break;
            }
            if (LbFileRead(fhandle, &game.packet_save_head, sizeof(struct PacketSaveHead))
                == sizeof(struct PacketSaveHead)) {
                chunks_done |= SGF_PacketHeader;
            } else {
                WARNLOG("Could not read GameOrig chunk");
            }
            break;
        case SGC_PacketData:
            if (hdr.len != 0)
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible PacketData chunk");
                break;
            }
            chunks_done |= SGF_PacketData;
            if ((chunks_done & SGF_PacketContinue) == SGF_PacketContinue)
                return GLoad_PacketContinue;
            if ((chunks_done & SGF_PacketStart) == SGF_PacketStart)
                return GLoad_PacketStart;
            return GLoad_Failed;
        case SGC_IntralevelData:
            if (hdr.len != sizeof(struct IntralevelData))
            {
                if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                    LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
                WARNLOG("Incompatible IntralevelData chunk");
                break;
            }
            if (LbFileRead(fhandle, &intralvl, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData)) {
                chunks_done |= SGF_IntralevelData;
            } else {
                WARNLOG("Could not read IntralevelData chunk");
            }
            break;
        case SGC_LuaData:
            {
                char* lua_data = (char*)malloc(hdr.len);
                if (lua_data == NULL) {
                    WARNLOG("Could not allocate memory for LuaData chunk");
                    break;
                }
                if (LbFileRead(fhandle, lua_data, hdr.len) == hdr.len) {
                    //has to be loaded here as level num only filled while gamestruct loaded, and need it for setting serialised_data
                    open_lua_script(get_loaded_level_number());

                    lua_set_serialised_data(lua_data, hdr.len);
                    chunks_done |= SGF_LuaData;
                } else {
                    WARNLOG("Could not read LuaData chunk");
                    free(lua_data);
                }
            }
            break;
        default:
            WARNLOG("Unrecognized chunk, ID = %08lx", hdr.id);
            if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
                LbFileSeek(fhandle, 0, Lb_FILE_SEEK_END);
            break;
        }
    }
    if ((chunks_done & SGF_SavedGame) == SGF_SavedGame)
    {
        // Update interface items
        update_trap_tab_to_config();
        update_room_tab_to_config();
        return GLoad_SavedGame;
    }
    return GLoad_Failed;
}

/**
 * Saves the game state file (savegame).
 * @note fill_game_catalogue_entry() should be called before to fill level information.
 *
 * @param slot_num
 * @return
 */
TbBool save_game(long slot_num)
{
    if (!ensure_catalogue_slot(slot_num))
    {
        ERRORLOG("Outranged slot index %d",(int)slot_num);
        return false;
    }
    char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
    TbFileHandle handle = LbFileOpen(fname, Lb_FILE_MODE_NEW);
    if (!handle)
    {
        WARNMSG("Cannot open file to save, \"%s\".",fname);
        return false;
    }
    if (!save_game_chunks(handle,&save_game_catalogue[slot_num]))
    {
        LbFileClose(handle);
        WARNMSG("Cannot write to save file, \"%s\".",fname);
        return false;
    }
    LbFileClose(handle);
    api_event("GAME_SAVED");
    return true;
}

TbBool is_save_game_loadable(long slot_num)
{
    // Prepare filename and open the file
    char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
    TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (fh)
    {
        // Let's try to read the file, just to be sure
        struct FileChunkHeader hdr;
        if (LbFileRead(fh, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        {
            LbFileClose(fh);
            return true;
        }
        LbFileClose(fh);
    }
    return false;
}

#define SAVE_TRASH_FALLBACK_DIR "trash"

/** Recoverable delete: on success, `save_game_catalogue[slot_num]` is always
 *  cleared, but the underlying file was moved, not erased.
 *
 *  Order of preference:
 *    1. TRASH_MAX_COUNT=0 -- trashing is disabled outright; delete straight
 *       through, same as the pre-trash behaviour this replaces.
 *    2. The OS-native trash (PlatformManager_TrashFile) -- freedesktop Trash on
 *       Linux, so the desktop's own file manager can restore it with no UI of
 *       ours. Declines itself (returns false, file untouched) under Flatpak, if
 *       the trash dir can't be created, or if the move fails for any reason.
 *    3. This fork's own retained trash, save/trash/. Filenames embed the
 *       deletion time so purge_save_trash_fallback() (run once at startup, not
 *       here) can enforce TRASH_MAX_COUNT / TRASH_MAX_DAYS without trusting
 *       mtime, which a copy or sync tool can rewrite. */
TbBool delete_save_game(long slot_num)
{
    // Bounded by the catalogue's logical length, not a fixed slot count: upstream
    // #5106 made the catalogue grow to fit the saves that exist, and only slots
    // inside that range are reachable from the menus in the first place.
    if ((slot_num < 0) || (slot_num >= save_game_catalogue_count))
    {
        ERRORLOG("Cannot delete save slot %d - out of range", (int)slot_num);
        return false;
    }
    // Copy out of prepare_file_fmtpath's shared static buffer immediately -- the
    // fallback path below calls it again to build the trash destination, which
    // would otherwise overwrite this string out from under us.
    char save_path[DISKPATH_SIZE * 2];
    snprintf(save_path, sizeof(save_path), "%s", prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num));

    if (save_trash_max_count == 0)
    {
        if (LbFileDelete(save_path) != 1)
        {
            WARNLOG("Cannot delete save file \"%s\"", save_path);
            return false;
        }
        memset(&save_game_catalogue[slot_num], 0, sizeof(struct CatalogueEntry));
        SYNCLOG("Deleted saved game in slot %d (trash disabled)", (int)slot_num);
        return true;
    }

    if (PlatformManager_TrashFile(save_path))
    {
        memset(&save_game_catalogue[slot_num], 0, sizeof(struct CatalogueEntry));
        SYNCLOG("Moved saved game in slot %d to the system trash", (int)slot_num);
        return true;
    }

    // Native trash unavailable (Flatpak sandbox, no Trash dir, cross-filesystem
    // rename, ...) -- fall back to our own retained trash.
    const char* base = strrchr(save_path, '/');
    base = (base != NULL) ? (base + 1) : save_path;
    TbTimeSec now = LbTimeSec();
    char fallback_dst[DISKPATH_SIZE * 2];
    snprintf(fallback_dst, sizeof(fallback_dst), "%s",
        prepare_file_fmtpath(FGrp_Save, SAVE_TRASH_FALLBACK_DIR "/%ld_%s", (long)now, base));
    if (!create_directory_for_file(fallback_dst) || (rename(save_path, fallback_dst) != 0))
    {
        WARNLOG("Cannot move save file \"%s\" to trash", save_path);
        return false;
    }
    memset(&save_game_catalogue[slot_num], 0, sizeof(struct CatalogueEntry));
    SYNCLOG("Moved saved game in slot %d to local trash (\"%s\")", (int)slot_num, fallback_dst);
    return true;
}

/** Fallback-trash entry parsed from a filename this fork wrote itself
 *  ("<epoch>_<original name>"). */
struct TrashEntry {
    long stamp;
    char name[DISKPATH_SIZE];
};

/** Parse the "<epoch>_" prefix delete_save_game() writes on every fallback-trash
 *  file. Returns -1 for anything that doesn't match -- e.g. a file a user
 *  dropped into save/trash/ by hand -- so purge leaves it alone rather than
 *  guessing at its age. */
static long trash_entry_stamp(const char* name)
{
    char* end = NULL;
    long stamp = strtol(name, &end, 10);
    if ((end == name) || (*end != '_'))
        return -1;
    return stamp;
}

void purge_save_trash_fallback(void)
{
    if (save_trash_max_count == 0)
        return; // trash is disabled outright; delete_save_game never wrote a fallback entry
    char* spec = prepare_file_path(FGrp_Save, SAVE_TRASH_FALLBACK_DIR "/*");
    struct TbFileEntry fe;
    struct TbFileFind* ff = LbFileFindFirst(spec, &fe);
    if (ff == NULL)
        return; // no fallback trash directory yet, or it's empty

    struct TrashEntry* entries = NULL;
    long count = 0;
    long capacity = 0;
    do {
        long stamp = trash_entry_stamp(fe.Filename);
        if (stamp < 0)
            continue; // not one of ours -- leave it alone
        if (count >= capacity)
        {
            capacity = (capacity == 0) ? 16 : (capacity * 2);
            struct TrashEntry* grown = (struct TrashEntry*)realloc(entries, capacity * sizeof(struct TrashEntry));
            if (grown == NULL)
            {
                ERRORLOG("Cannot grow trash purge list; some old entries may not be purged this run");
                break;
            }
            entries = grown;
        }
        entries[count].stamp = stamp;
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", fe.Filename);
        count++;
    } while (LbFileFindNext(ff, &fe) >= 0);
    LbFileFindEnd(ff);

    if (count == 0)
    {
        free(entries);
        return;
    }

    // Oldest first, so eviction below can walk from index 0 and stop as soon as
    // an entry is within both limits (nothing later in the list can be older).
    for (long i = 1; i < count; i++)
    {
        struct TrashEntry key = entries[i];
        long j = i - 1;
        while ((j >= 0) && (entries[j].stamp > key.stamp))
        {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }

    long excess = count - save_trash_max_count;
    if (excess < 0)
        excess = 0;
    TbTimeSec now = LbTimeSec();
    long evicted = 0;
    for (long i = 0; i < count; i++)
    {
        TbBool over_count = (i < excess);
        TbBool over_age = (save_trash_max_days > 0) &&
            (((long)now - entries[i].stamp) > (save_trash_max_days * 86400L));
        if (!over_count && !over_age)
            break;
        char* fname = prepare_file_fmtpath(FGrp_Save, SAVE_TRASH_FALLBACK_DIR "/%s", entries[i].name);
        if (LbFileDelete(fname) == 1)
        {
            SYNCLOG("Purged fallback trash entry \"%s\" (%s)", entries[i].name,
                over_count ? "over retention count" : "past retention age");
            evicted++;
        }
        else
        {
            WARNLOG("Cannot purge fallback trash entry \"%s\"", fname);
        }
    }
    if (evicted > 0)
        SYNCLOG("Fallback trash purge: evicted %ld of %ld entries", evicted, count);
    free(entries);
}

enum SaveLoadFailure last_save_load_failure = SaveLoadFail_None;

TbBool save_entry_from_other_build(const struct CatalogueEntry *centry)
{
    if (centry == NULL)
        return false;
    return (centry->game_ver_major != VER_MAJOR) || (centry->game_ver_minor != VER_MINOR) ||
           (centry->game_ver_release != VER_RELEASE) || (centry->game_ver_build != VER_BUILD);
}

TextStringId save_load_failure_stridx(void)
{
    // Resolved on every call rather than cached: campaigns, maps and mods may
    // each add their own translation.toml, and that renumbers the table.
    const char *alias = (last_save_load_failure == SaveLoadFail_Version)
                      ? "SAVE_LOAD_FAILED_VERSION" : "SAVE_LOAD_FAILED_UNREADABLE";
    TextStringId stridx = get_string_id_by_alias(alias);
    return (stridx > 0) ? stridx : GUIStr_Error;
}

const char *save_load_failure_text(void)
{
    const char *text = get_string(save_load_failure_stridx());
    return (text != NULL) ? text : "";
}

/** Pre-flight scan of a savegame's chunk table: is its game-state block the
 *  size this build compiles?
 *
 *  The save format embeds a raw dump of `struct Game`, so any change to that
 *  struct invalidates every existing save. load_game_chunks() only discovers
 *  that half way through - by then it has already switched campaign, reloaded
 *  the stats files and re-opened a Lua script, which is exactly the state we
 *  must not leave behind when the answer is "this save cannot be loaded".
 *  Walking the headers first costs a few seeks and lets the load be refused
 *  before anything global is touched.
 *
 *  Deliberately keyed on the chunk size, not on the version fields: a save from
 *  a different build whose struct Game happens to be unchanged still loads
 *  fine today, and refusing it on the version number alone would be a
 *  regression. The file position is left undefined; the caller seeks back. */
static TbBool save_file_state_chunk_fits(TbFileHandle fhandle)
{
    if (LbFileSeek(fhandle, 0, Lb_FILE_SEEK_BEGINNING) < 0)
        return false;
    while (!LbFileEof(fhandle))
    {
        struct FileChunkHeader hdr;
        if (LbFileRead(fhandle, &hdr, sizeof(struct FileChunkHeader)) != sizeof(struct FileChunkHeader))
            break;
        if (hdr.id == SGC_GameOrig)
            return (hdr.len == sizeof(struct Game));
        if (LbFileSeek(fhandle, hdr.len, Lb_FILE_SEEK_CURRENT) < 0)
            break;
    }
    // No game-state chunk at all: not a savegame we can restore.
    return false;
}

TbBool load_game(long slot_num)
{
    last_save_load_failure = SaveLoadFail_Unreadable;
    if (!ensure_catalogue_slot(slot_num))
    {
        ERRORLOG("Outranged slot index %d",(int)slot_num);
        return false;
    }
    TbFileHandle fh;
//  unsigned char buf[14];
//  char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    SYNCDBG(6,"Starting");
    {
        // Use fname only here - it is overwritten by next use of prepare_file_fmtpath()
        char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
        fh = LbFileOpen(fname,Lb_FILE_MODE_READ_ONLY);
        if (!fh)
        {
          WARNMSG("Cannot open saved game file \"%s\".",fname);
          save_catalogue_slot_disable(slot_num);
          return false;
        }
    }
    long file_len = LbFileLengthHandle(fh);
    if (is_primitive_save_version(file_len))
    {
        {
          LbFileClose(fh);
          save_catalogue_slot_disable(slot_num);
          return false;
        }
    }
    struct CatalogueEntry* centry = &save_game_catalogue[slot_num];

    TbBool other_build = save_entry_from_other_build(centry);
    if (other_build)
    {
        WARNLOG("loading savegame made in different version %d.%d.%d.%d current %d.%d.%d.%d",
            (int)centry->game_ver_major, (int)centry->game_ver_minor,
            (int)centry->game_ver_release, (int)centry->game_ver_build,
            VER_MAJOR, VER_MINOR, VER_RELEASE, VER_BUILD);
    }
    // Refuse an unloadable save before any global state is disturbed, so the
    // caller can put the player back in the menu with the session intact.
    if (!save_file_state_chunk_fits(fh))
    {
        LbFileClose(fh);
        last_save_load_failure = other_build ? SaveLoadFail_Version : SaveLoadFail_Unreadable;
        WARNMSG("Saved game in slot %d cannot be loaded by this build (%s).",(int)slot_num,
            other_build ? "made by another version" : "unusable game state block");
        return false;
    }

    reset_eye_lenses();
    LbFileSeek(fh, 0, Lb_FILE_SEEK_BEGINNING);
    // Here is the actual loading
    if (load_game_chunks(fh,centry) != GLoad_SavedGame)
    {
        LbFileClose(fh);
        if (game.loaded_level_number == 0)
        {
            game.loaded_level_number = centry->level_num;
        }
        last_save_load_failure = other_build ? SaveLoadFail_Version : SaveLoadFail_Unreadable;
        WARNMSG("Couldn't correctly load saved game in slot %d.",(int)slot_num);
        return false;
    }
    last_save_load_failure = SaveLoadFail_None;
    my_player_number = game.local_plyr_idx;
    LbFileClose(fh);
    // Re-apply creature sound overrides: SGC_GameOrig restored game.conf with
    // session-specific negative bank indices from the save; fix them to match
    // the current session's custom bank layout.
    sound_manager_reapply_creature_sounds();
    snprintf(game.campaign_fname, sizeof(game.campaign_fname), "%s", campaign.fname);
    reinit_level_after_load();
    initialize_packet_history();
    clear_packets();
    process_pause_packet(0, 0);
    clear_flag(game.operation_flags, GOF_Paused);
    clear_flag(game.operation_flags, GOF_WorldInfluence);
    close_main_cheat_menu();
    close_creature_cheat_menu();
    close_instance_cheat_menu();
    close_secondary_cheat_menu();
    output_message(SMsg_GameLoaded, 0);
    panel_map_update(0, 0, game.map_subtiles_x+1, game.map_subtiles_y+1);
    calculate_moon_phase(false,false);
    update_extra_levels_visibility();
    struct PlayerInfo* player = get_my_player();
    clear_flag(player->additional_flags, PlaAF_LightningPaletteIsActive);
    clear_flag(player->additional_flags, PlaAF_FreezePaletteIsActive);
    player->palette_fade_step_pain = 0;
    player->palette_fade_step_possession = 0;
    player->lens_palette = 0;
    // Reinitialize lens first (restores lens_palette pointer from config)
    reinitialise_eye_lens(game.applied_lens_type);
    // Apply the appropriate palette (lens palette if active, otherwise engine default)
    PaletteSetPlayerPalette(player, player->lens_palette ? player->lens_palette : engine_palette);
    init_local_cameras(player);
    // Update the lights system state
    light_import_system_state(&game.lightst);
    // Victory state
    if (player->victory_state != VicS_Undecided)
    {
      frontstats_initialise();
      struct Dungeon* dungeon = get_players_dungeon(player);
      dungeon->lvstats.player_score = 0;
      dungeon->lvstats.allow_save_score = 1;
    }
    game.loaded_swipe_idx = -1;
    JUSTMSG("Loaded level %d from %s", game.continue_level_number, campaign.name);

    api_event("GAME_LOADED");

    return true;
}

int count_valid_saved_games(void)
{
  number_of_saved_games = 0;
  for (int i = 0; i < save_game_catalogue_count; i++)
  {
      struct CatalogueEntry* centry = &save_game_catalogue[i];
      if ((centry->flags & CEF_InUse) != 0)
          number_of_saved_games++;
  }
  return number_of_saved_games;
}

TbBool fill_game_catalogue_entry(struct CatalogueEntry *centry,const char *textname)
{
    centry->level_num = get_loaded_level_number();
    snprintf(centry->textname, SAVE_TEXTNAME_LEN, "%s", textname);
    snprintf(centry->campaign_name, LINEMSG_SIZE, "%s", campaign.name);
    snprintf(centry->campaign_fname, DISKPATH_SIZE, "%s", campaign.fname);
    snprintf(centry->player_name, PLAYER_NAME_LENGTH, "%s", high_score_entry);
    set_flag(centry->flags, CEF_InUse);
    centry->game_ver_major = VER_MAJOR;
    centry->game_ver_minor = VER_MINOR;
    centry->game_ver_release = VER_RELEASE;
    centry->game_ver_build = VER_BUILD;
    return true;
}

TbBool fill_game_catalogue_slot(long slot_num,const char *textname)
{
    if (!ensure_catalogue_slot(slot_num))
    {
        ERRORLOG("Outranged slot index %d",(int)slot_num);
        return false;
    }
    struct CatalogueEntry* centry = &save_game_catalogue[slot_num];
    return fill_game_catalogue_entry(centry,textname);
}

TbBool game_catalogue_slot_disable(struct CatalogueEntry *game_catalg,unsigned int slot_idx)
{
  if (slot_idx >= (unsigned int)save_game_catalogue_count)
    return false;
  clear_flag(game_catalg[slot_idx].flags, CEF_InUse);
  return true;
}

TbBool save_catalogue_slot_disable(unsigned int slot_idx)
{
  return game_catalogue_slot_disable(save_game_catalogue,slot_idx);
}

TbBool load_catalogue_entry(TbFileHandle fh,struct FileChunkHeader *hdr,struct CatalogueEntry *centry)
{
    clear_flag(centry->flags, CEF_InUse);
    if ((hdr->id == SGC_InfoBlock) && (hdr->len == sizeof(struct CatalogueEntry)))
    {
        if (LbFileRead(fh, centry, sizeof(struct CatalogueEntry))
          == sizeof(struct CatalogueEntry))
        {
            set_flag(centry->flags, CEF_InUse);
        }
    }
    centry->textname[SAVE_TEXTNAME_LEN-1] = '\0';
    centry->campaign_name[LINEMSG_SIZE-1] = '\0';
    centry->campaign_fname[DISKPATH_SIZE-1] = '\0';
    centry->player_name[PLAYER_NAME_LENGTH-1] = '\0';
    return ((centry->flags & CEF_InUse) != 0);
}


TbBool load_game_save_catalogue(void)
{
    // Scan the save directory to find the highest existing slot index, so the catalogue
    // can be sized to exactly the saves that exist plus one free slot to save into.
    long highest = -1;
    struct TbFileEntry fe;
    char* spec = prepare_file_path(FGrp_Save, "fx1g*.sav");
    struct TbFileFind* ff = LbFileFindFirst(spec, &fe);
    if (ff != NULL)
    {
        do {
            int idx = save_slot_index_from_filename(fe.Filename);
            if (idx > highest)
                highest = idx;
        } while (LbFileFindNext(ff, &fe) >= 0);
        LbFileFindEnd(ff);
    }
    long needed = highest + 2;               // used slots + one free slot to save into
    if (needed < SAVE_SLOTS_MIN)
        needed = SAVE_SLOTS_MIN;
    if (needed > SAVE_SLOTS_LIMIT)
        needed = SAVE_SLOTS_LIMIT;
    if (!ensure_catalogue_slot(needed - 1))
        return false;
    save_game_catalogue_count = needed;      // logical length the menus range over

    // (Re)load metadata for every slot in range; missing files leave a zeroed (free) entry.
    long saves_found = 0;
    for (long slot_num = 0; slot_num < save_game_catalogue_count; slot_num++)
    {
        struct CatalogueEntry* centry = &save_game_catalogue[slot_num];
        memset(centry, 0, sizeof(struct CatalogueEntry));
        char* fname = prepare_file_fmtpath(FGrp_Save, saved_game_filename, slot_num);
        TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
        if (!fh)
            continue;
        struct FileChunkHeader hdr;
        if (LbFileRead(fh, &hdr, sizeof(struct FileChunkHeader)) == sizeof(struct FileChunkHeader))
        {
            if (load_catalogue_entry(fh,&hdr,centry))
                saves_found++;
        }
        LbFileClose(fh);
    }
    return (saves_found > 0);
}

TbBool initialise_load_game_slots(void)
{
    load_game_save_catalogue();
    return (count_valid_saved_games() > 0);
}

short save_continue_game(LevelNumber lvnum)
{
    // Update continue level number
    if (is_singleplayer_like_level(lvnum))
      set_continue_level_number(lvnum);
    SYNCDBG(6,"Continue set to level %d (loaded is %d)",(int)get_continue_level_number(),(int)get_loaded_level_number());
    char* fname = prepare_file_path(FGrp_Save, continue_game_filename);
    TbFileHandle fh = LbFileOpen(fname,Lb_FILE_MODE_NEW);
    if (!fh)
    {
        WARNMSG("Cannot open continue game file \"%s\".", fname);
        return false;
    }
    char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    memset(cmpgn_fname, 0, sizeof(cmpgn_fname));
    snprintf(cmpgn_fname, sizeof(cmpgn_fname), "%s", campaign.fname);
    LevelNumber continue_level_number = get_continue_level_number();
    short result = false;
    if (LbFileWrite(fh, cmpgn_fname, sizeof(cmpgn_fname)) == sizeof(cmpgn_fname))
    if (LbFileWrite(fh, &continue_level_number, sizeof(continue_level_number)) == sizeof(continue_level_number))
    // Appending IntralevelData
    if (LbFileWrite(fh, &intralvl, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData))
        result = true;
    LbFileClose(fh);
    return result;
}

static short read_continue_game_progress(char *cmpgn_fname, LevelNumber *lvnum, struct IntralevelData *intralevel)
{
    char* fname = prepare_file_path(FGrp_Save, continue_game_filename);
    int32_t fsize = LbFileLength(fname);
    if (fsize != (int32_t)CONTINUE_GAME_FILE_SIZE)
    {
        SYNCDBG(7, "No correct .SAV file; there's no continue");
        return false;
    }
    TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (!fh)
    {
        SYNCDBG(7,"Can't open .SAV file; there's no continue");
        return false;
    }
    short result = false;
    if (LbFileRead(fh, cmpgn_fname, CAMPAIGN_FNAME_LEN) == CAMPAIGN_FNAME_LEN)
    if (LbFileRead(fh, lvnum, sizeof(*lvnum)) == sizeof(*lvnum))
    if (LbFileRead(fh, intralevel, sizeof(struct IntralevelData)) == sizeof(struct IntralevelData))
        result = true;
    LbFileClose(fh);
    if (!result)
    {
        SYNCDBG(7, "No correct .SAV file; there's no continue");
        return false;
    }
    cmpgn_fname[CAMPAIGN_FNAME_LEN-1] = '\0';
    return true;
}

/**
 * Indicates whether continue game option is available.
 * @return
 */
TbBool continue_game_available(void)
{
    LevelNumber lvnum;
    SYNCDBG(6,"Starting");
    char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    struct IntralevelData intralevel;
    if (!read_continue_game_progress(cmpgn_fname, &lvnum, &intralevel)) {
        WARNLOG("Can't read continue game file head");
        return false;
    }
    if (!change_campaign(CampgnT_Campaign, cmpgn_fname))
    {
        ERRORLOG("Unable to load campaign");
        return false;
    }
    if (!is_singleplayer_like_level(lvnum))
    {
        SYNCDBG(7,"Level %d from continue file is not single player",(int)lvnum);
        return false;
    }
    set_continue_level_number(lvnum);
    SYNCDBG(7,"Continue to level %d is available",(int)lvnum);
    return true;
}

short load_continue_game(void)
{
    LevelNumber lvnum;
    char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    struct IntralevelData intralevel;
    if (!read_continue_game_progress(cmpgn_fname, &lvnum, &intralevel)) {
        WARNLOG("Can't read continue game file head");
        return false;
    }
    if (!change_campaign(CampgnT_Campaign, cmpgn_fname))
    {
        ERRORLOG("Unable to load campaign");
        return false;
    }
    if (!is_singleplayer_like_level(lvnum))
    {
      WARNLOG("Level number in continue file is incorrect");
      return false;
    }
    set_continue_level_number(lvnum);
    // Restoring intralevel data
    memcpy(&intralvl, &intralevel, sizeof(struct IntralevelData));
    snprintf(game.campaign_fname, sizeof(game.campaign_fname), "%s", campaign.fname);
    update_extra_levels_visibility();
    JUSTMSG("Continued level %d from %s", lvnum, campaign.name);
    return true;
}

TbBool add_transfered_creature(PlayerNumber plyr_idx, ThingModel model, CrtrExpLevel exp_level, char *name)
{
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon))
    {
        ERRORDBG(11, "Can't transfer creature; player %d has no dungeon.", (int)plyr_idx);
        return false;
    }

    short i = dungeon->creatures_transferred; //makes sure it fits 255 units

    intralvl.transferred_creatures[plyr_idx][i].model = model;
    intralvl.transferred_creatures[plyr_idx][i].exp_level = exp_level;
    strcpy(intralvl.transferred_creatures[plyr_idx][i].creature_name, name);
    return true;
}

void clear_transfered_creatures(void)
{
    for (int p = 0; p < PLAYERS_COUNT; p++)
    {
        for (int i = 0; i < TRANSFER_CREATURE_STORAGE_COUNT; i++)
        {
            intralvl.transferred_creatures[p][i].model = 0;
            intralvl.transferred_creatures[p][i].exp_level = 0;
        }
    }
}

LevelNumber move_campaign_to_next_level(void)
{
    LevelNumber curr_lvnum = get_continue_level_number();
    LevelNumber lvnum = next_singleplayer_level(curr_lvnum, false);
    SYNCDBG(15,"Campaign move %d to %d",curr_lvnum,lvnum);
    {
        struct PlayerInfo* player = get_my_player();
        player->display_flags &= ~PlaF6_PlyrHasQuit;
    }
    if (lvnum != LEVELNUMBER_ERROR)
    {
        curr_lvnum = set_continue_level_number(lvnum);
        SYNCDBG(8,"Continue level moved to %d.",curr_lvnum);
        return curr_lvnum;
    } else
    {
        curr_lvnum = set_continue_level_number(SINGLEPLAYER_NOTSTARTED);
        SYNCDBG(8,"Continue level moved to NOTSTARTED.");
        return curr_lvnum;
    }
}

LevelNumber move_campaign_to_prev_level(void)
{
    LevelNumber curr_lvnum = get_continue_level_number();
    LevelNumber lvnum = prev_singleplayer_level(curr_lvnum);
    SYNCDBG(15,"Campaign move %d to %d",curr_lvnum,lvnum);
    if (lvnum != LEVELNUMBER_ERROR)
    {
        curr_lvnum = set_continue_level_number(lvnum);
        SYNCDBG(8,"Continue level moved to %d.",curr_lvnum);
        return curr_lvnum;
    } else
    {
        curr_lvnum = set_continue_level_number(SINGLEPLAYER_FINISHED);
        SYNCDBG(8,"Continue level moved to FINISHED.");
        return curr_lvnum;
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
