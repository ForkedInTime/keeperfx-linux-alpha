/******************************************************************************/
/** @file legacy_progress_lp64.h
 *     Header file for legacy_progress_lp64.c.
 * @par Purpose:
 *     Reading fx1contn.sav as 64-bit Linux builds wrote it.
 * @par Comment:
 *     This edition's own, used by the legacy import in kfx/save/SaveContinue.cpp.
 */
/******************************************************************************/
/*      This program is free software; you can redistribute it and/or modify  */
/*      it under the terms of the GNU General Public License as published by  */
/*      the Free Software Foundation; either version 2 of the License, or     */
/*      (at your option) any later version.                                   */
/*                                                                            */
/******************************************************************************/
#ifndef DK_LEGACY_PROGRESS_LP64_H
#define DK_LEGACY_PROGRESS_LP64_H

#include <stddef.h>
#include <stdint.h>
#include "game_merge.h"
#include "config_campaigns.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/*
 * fx1contn.sav is a raw dump of struct IntralevelData's version 0 prefix, so its
 * layout followed the compiler's `long`. campaign_flags were `long` until upstream
 * made them int32_t in the change that split progress into per-campaign files: 4
 * bytes on upstream's 32-bit Windows build, which the legacy import describes, but
 * 8 here. Every fx1contn.sav a 64-bit Linux build wrote -- stable 1.4.0.5660
 * included -- is therefore 160 bytes longer, fails that size check, and the
 * player's campaign position would be dropped without a word. That layout is the
 * v0 one with each flag widened to 8 bytes; it is read here and narrowed.
 */
// The v0 prefix: everything before continue_level (INTRALEVEL_DATA_V0_SIZE upstream)
#define LEGACY_LP64_V0_SIZE            offsetof(struct IntralevelData, continue_level)
#define LEGACY_LP64_FLAGS_COUNT        (PLAYERS_FOR_CAMPAIGN_FLAGS * CAMPAIGN_FLAGS_PER_PLAYER)
#define LEGACY_LP64_INTRALEVEL_SIZE    (LEGACY_LP64_V0_SIZE + LEGACY_LP64_FLAGS_COUNT * (sizeof(int64_t) - sizeof(int32_t)))
#define LEGACY_LP64_PROGRESS_FILE_SIZE (CAMPAIGN_FNAME_LEN + sizeof(LevelNumber) + LEGACY_LP64_INTRALEVEL_SIZE)

/** Narrows a 64-bit-long IntralevelData v0 dump (LEGACY_LP64_INTRALEVEL_SIZE bytes)
 *  into ilvl. Bytes of ilvl past the v0 prefix are left as they are. */
void decode_lp64_intralevel_v0(const unsigned char *raw, struct IntralevelData *ilvl);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
