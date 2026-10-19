/******************************************************************************/
/** @file save_trash.h
 *     Header file for save_trash.c.
 * @par Purpose:
 *     Deleting saved games recoverably, into the system trash or save/trash/.
 * @par Comment:
 *     This edition's own, kept out of the save files upstream edits.
 */
/******************************************************************************/
/*      This program is free software; you can redistribute it and/or modify  */
/*      it under the terms of the GNU General Public License as published by  */
/*      the Free Software Foundation; either version 2 of the License, or     */
/*      (at your option) any later version.                                   */
/*                                                                            */
/******************************************************************************/
#ifndef DK_SAVE_TRASH_H
#define DK_SAVE_TRASH_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Fallback-trash retention (save/trash/, used only when the OS-native trash is
 *  unavailable -- see delete_save_game()), from keeperfx.cfg TRASH_MAX_COUNT and
 *  TRASH_MAX_DAYS. A count of 0 disables trashing outright: deletes go straight
 *  through, no trash of any kind. */
extern long save_trash_max_count;
extern long save_trash_max_days;

/** Moves the saved game in a slot to the trash and frees the slot. */
TbBool delete_save_game(long slot_num);
/** Moves a save this build cannot load out of the way before the slot is saved
 *  into, so a game a different version can still open is not overwritten.
 *  Returns true when the slot is free to write. */
TbBool trash_unloadable_save(long slot_num);
/** Evict fallback-trash entries (save/trash/) past the configured retention,
 *  oldest first. Call once at startup -- never on the delete path itself, or
 *  the clock only ever advances when someone happens to delete a save. Native
 *  OS trash is out of scope: this only ever touches save/trash/. */
void purge_save_trash_fallback(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
