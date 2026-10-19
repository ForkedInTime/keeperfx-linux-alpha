/******************************************************************************/
/** @file save_trash.c
 *     Deleting saved games recoverably.
 * @par Purpose:
 *     Moves saved games to the system trash, or to save/trash/ where there is
 *     none, and keeps save/trash/ to its configured size and age.
 * @par Comment:
 *     This edition's own. It sits beside the save code upstream edits rather
 *     than in it, and reaches the catalogue only through SaveManager.
 */
/******************************************************************************/
/*      This program is free software; you can redistribute it and/or modify  */
/*      it under the terms of the GNU General Public License as published by  */
/*      the Free Software Foundation; either version 2 of the License, or     */
/*      (at your option) any later version.                                   */
/*                                                                            */
/******************************************************************************/
#include "pre_inc.h"
#include "save_trash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_datetm.h"
#include "config.h"
#include "kfx/platform/PlatformManager.h"
#include "kfx/save/SaveManager.h"
#include "post_inc.h"

/******************************************************************************/
// The name SaveManager gives a slot's file (SAVED_GAME_FILENAME in its internal header)
#define SAVE_TRASH_SLOT_FILENAME "fx1g%04d.sav"
#define SAVE_TRASH_FALLBACK_DIR "trash"

long save_trash_max_count = 10;
long save_trash_max_days = 30;

/******************************************************************************/
/** The slot's file as it is really named on disk. The catalogue scan and the
 *  file opens match "fx1g%04d.sav" case-insensitively, so a FX1G0003.SAV copied
 *  from a DOS/GOG install lists and loads -- and rename() must move that file,
 *  not a lower-case name that does not exist. */
static void slot_file_path(long slot_num, char *buf, size_t buflen)
{
    LbFileCaseInsensitivePath(prepare_file_fmtpath(FGrp_Save, SAVE_TRASH_SLOT_FILENAME, (int)slot_num), buf, buflen);
}

/** Recoverable delete of one file: on success it was moved, not erased.
 *
 *  Order of preference:
 *    1. TRASH_MAX_COUNT=0 -- trashing is disabled outright; delete straight
 *       through.
 *    2. The OS-native trash (PlatformManager_TrashFile) -- freedesktop Trash on
 *       Linux, so the desktop's own file manager can restore it with no UI of
 *       ours. Declines itself (returns false, file untouched) under Flatpak, if
 *       the trash dir can't be created, or if the move fails for any reason.
 *    3. This edition's own retained trash, save/trash/. Filenames embed the
 *       deletion time so purge_save_trash_fallback() (run once at startup, not
 *       here) can enforce TRASH_MAX_COUNT / TRASH_MAX_DAYS without trusting
 *       mtime, which a copy or sync tool can rewrite. A file already there is
 *       never replaced. */
static TbBool trash_file(const char *save_path, long slot_num)
{
    if (save_trash_max_count == 0)
    {
        if (LbFileDelete(save_path) != 1)
        {
            WARNLOG("Cannot delete save file \"%s\"", save_path);
            return false;
        }
        SYNCLOG("Deleted saved game in slot %d (trash disabled)", (int)slot_num);
        return true;
    }
    if (PlatformManager_TrashFile(save_path))
    {
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
    if (LbFileExists(fallback_dst))
    {
        WARNLOG("Cannot move save file \"%s\" to trash: \"%s\" is already there", save_path, fallback_dst);
        return false;
    }
    if (!create_directory_for_file(fallback_dst) || (rename(save_path, fallback_dst) != 0))
    {
        WARNLOG("Cannot move save file \"%s\" to trash", save_path);
        return false;
    }
    SYNCLOG("Moved saved game in slot %d to local trash (\"%s\")", (int)slot_num, fallback_dst);
    return true;
}

TbBool delete_save_game(long slot_num)
{
    // Bounded by the catalogue's logical length: the catalogue grows to fit the
    // saves that exist, and only slots inside that range are reachable from the
    // menus in the first place.
    if ((slot_num < 0) || (slot_num >= SaveManager_SlotCount()))
    {
        ERRORLOG("Cannot delete save slot %d - out of range", (int)slot_num);
        return false;
    }
    char save_path[DISKPATH_SIZE * 2];
    slot_file_path(slot_num, save_path, sizeof(save_path));
    if (!trash_file(save_path, slot_num))
        return false;
    SaveManager_DisableSlot((int)slot_num);
    return true;
}

TbBool trash_unloadable_save(long slot_num)
{
    // Saving over a save this build can read is an ordinary overwrite. One it
    // cannot read (made by an older version, or damaged) may still open in the
    // version that wrote it, so it goes to the trash instead of being lost.
    enum SaveCheckResult chk = SaveManager_Check((int)slot_num, NULL, 0);
    if ((chk == SvChk_Loadable) || (chk == SvChk_Missing))
        return true;
    if (save_trash_max_count == 0)
        return true; // the player turned the trash off; the save simply replaces it
    char save_path[DISKPATH_SIZE * 2];
    slot_file_path(slot_num, save_path, sizeof(save_path));
    if (!LbFileExists(save_path))
        return true;
    return trash_file(save_path, slot_num);
}

/** Fallback-trash entry parsed from a filename this edition wrote itself
 *  ("<epoch>_<original name>"). */
struct TrashEntry {
    long stamp;
    char name[DISKPATH_SIZE];
};

/** Parse the "<epoch>_" prefix trash_file() writes on every fallback-trash
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
        return; // trash is disabled outright; trash_file never wrote a fallback entry
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
/******************************************************************************/
