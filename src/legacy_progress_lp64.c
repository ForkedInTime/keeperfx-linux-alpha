/******************************************************************************/
/** @file legacy_progress_lp64.c
 *     Reading fx1contn.sav as 64-bit Linux builds wrote it.
 * @par Purpose:
 *     Narrows the 8-byte campaign flags of that layout into IntralevelData.
 * @par Comment:
 *     This edition's own; see legacy_progress_lp64.h.
 */
/******************************************************************************/
/*      This program is free software; you can redistribute it and/or modify  */
/*      it under the terms of the GNU General Public License as published by  */
/*      the Free Software Foundation; either version 2 of the License, or     */
/*      (at your option) any later version.                                   */
/*                                                                            */
/******************************************************************************/
#include "pre_inc.h"
#include "legacy_progress_lp64.h"

#include <string.h>
#include "post_inc.h"

/******************************************************************************/
_Static_assert(sizeof(((struct IntralevelData *)0)->campaign_flags) == LEGACY_LP64_FLAGS_COUNT * sizeof(int32_t)
    && offsetof(struct IntralevelData, next_level) == offsetof(struct IntralevelData, campaign_flags) + LEGACY_LP64_FLAGS_COUNT * sizeof(int32_t),
    "decode_lp64_intralevel_v0() assumes int32_t campaign_flags directly followed by next_level");
// The size those builds wrote, measured from their headers. The files on disk
// do not change when a struct inside IntralevelData does: if this fails, such a
// change needs its own decoding here, or 64-bit players lose their progress.
_Static_assert(LEGACY_LP64_PROGRESS_FILE_SIZE == 67401,
    "IntralevelData's v0 prefix changed size; fx1contn.sav files from 64-bit builds no longer match it");

void decode_lp64_intralevel_v0(const unsigned char *raw, struct IntralevelData *ilvl)
{
    const size_t flags_at = offsetof(struct IntralevelData, campaign_flags);
    const size_t rest_at = offsetof(struct IntralevelData, next_level);
    memcpy(ilvl, raw, flags_at);
    for (int i = 0; i < PLAYERS_FOR_CAMPAIGN_FLAGS; i++)
    {
        for (int k = 0; k < CAMPAIGN_FLAGS_PER_PLAYER; k++)
        {
            int64_t val;
            memcpy(&val, raw + flags_at + (size_t)(i * CAMPAIGN_FLAGS_PER_PLAYER + k) * sizeof(int64_t), sizeof(val));
            // Clamped rather than wrapped, so a large counter keeps its sign.
            if (val > INT32_MAX)
                val = INT32_MAX;
            if (val < INT32_MIN)
                val = INT32_MIN;
            ilvl->campaign_flags[i][k] = (int32_t)val;
        }
    }
    memcpy((unsigned char *)ilvl + rest_at, raw + flags_at + LEGACY_LP64_FLAGS_COUNT * sizeof(int64_t),
        LEGACY_LP64_V0_SIZE - rest_at);
}
/******************************************************************************/
