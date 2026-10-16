/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file highscores.c
 *
 * @par Purpose:
 * @par Comment:
 *     None.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "bflib_dernc.h"

#include "highscores.h"
#include "globals.h"
#include "config.h"
#include "config_campaigns.h"


#include "post_inc.h"

/******************************************************************************/
// The score file is the table written raw, the same file on Windows and Linux.
_Static_assert(sizeof(struct HighScore) == 72, "high score file entries are 72 bytes");

/**
 * Entry size in the score files of earlier 64-bit Linux builds, where `score` was an
 * 8-byte long: score at 0 (8 bytes), name at 8, lvnum at 72, padding to 80.
 */
#define HISCORE_ENTRY_SIZE_LP64 80

/**
 * Loads a score file written in the earlier 64-bit Linux layout, converting each
 * entry, so those players keep their scores.
 */
static TbBool load_high_score_table_lp64(const char *fname, long file_len)
{
    if (campaign.hiscore_table == NULL)
        return false;
    unsigned char *buf = (unsigned char *)malloc(file_len);
    if (buf == NULL)
        return false;
    TbBool loaded = (LbFileLoadAt(fname, buf) == file_len);
    if (loaded)
    {
        for (unsigned long i = 0; i < campaign.hiscore_count; i++)
        {
            const unsigned char *src = buf + i * HISCORE_ENTRY_SIZE_LP64;
            struct HighScore *hscore = &campaign.hiscore_table[i];
            int64_t score;
            memcpy(&score, src, sizeof(score));
            hscore->score = (score > INT32_MAX) ? INT32_MAX : ((score < INT32_MIN) ? INT32_MIN : (int32_t)score);
            memcpy(hscore->name, src + 8, HISCORE_NAME_LENGTH);
            hscore->name[HISCORE_NAME_LENGTH - 1] = '\0';
            memcpy(&hscore->lvnum, src + 8 + HISCORE_NAME_LENGTH, sizeof(hscore->lvnum));
        }
        SYNCMSG("High scores table %s is in the earlier 64-bit layout; converted.", fname);
    }
    free(buf);
    return loaded;
}

static TbBool load_high_score_table(void)
{
    char* fname = prepare_file_path(FGrp_Save, campaign.hiscore_fname);
    long arr_size = campaign.hiscore_count * sizeof(struct HighScore);
    if (arr_size <= 0)
    {
        free(campaign.hiscore_table);
        campaign.hiscore_table = NULL;
        return true;
    }
    if (campaign.hiscore_table == NULL)
        campaign.hiscore_table = (struct HighScore *)calloc(arr_size, 1);
    long file_len = LbFileLengthRnc(fname);
    if ((file_len != arr_size) && (file_len == (long)(campaign.hiscore_count * HISCORE_ENTRY_SIZE_LP64)))
        return load_high_score_table_lp64(fname, file_len);
    if (file_len != arr_size)
        return false;
    if (campaign.hiscore_table == NULL)
        return false;
    if (LbFileLoadAt(fname, campaign.hiscore_table) == arr_size)
        return true;
    return false;
}

/**
 * Generates new high score table if previous can't be loaded.
 */
static TbBool create_empty_high_score_table(void)
{
  int i;
  int npoints = 100 * VISIBLE_HIGH_SCORES_COUNT;
  int nmap = 1 * VISIBLE_HIGH_SCORES_COUNT;
  long arr_size = campaign.hiscore_count * sizeof(struct HighScore);
  if (campaign.hiscore_table == NULL)
    campaign.hiscore_table = (struct HighScore *)calloc(arr_size, 1);
  if (campaign.hiscore_table == NULL)
    return false;
  for (i=0; i < VISIBLE_HIGH_SCORES_COUNT; i++)
  {
    if (i >= campaign.hiscore_count) break;
    snprintf(campaign.hiscore_table[i].name, sizeof(campaign.hiscore_table[0].name), "Bullfrog");
    campaign.hiscore_table[i].score = npoints;
    campaign.hiscore_table[i].lvnum = nmap;
    npoints -= 100;
    nmap -= 1;
  }
  while (i < campaign.hiscore_count)
  {
    campaign.hiscore_table[i].name[0] = '\0';
    campaign.hiscore_table[i].score = 0;
    campaign.hiscore_table[i].lvnum = 0;
    i++;
  }
  return true;
}

void load_or_create_high_score_table(void)
{
  if (!load_high_score_table())
  {
     SYNCMSG("High scores table bad; creating new one.");
     create_empty_high_score_table();
     save_high_score_table();
  }
}

/**
 * Saves the table in the earlier 64-bit Linux layout (see load_high_score_table_lp64).
 *
 * Stable and alpha share one game folder, and stable 1.4.0.5660 reads only this
 * layout: given a 72-byte table it decides the file is bad and writes a fresh
 * default table over it, so a player switching back to stable lost every score.
 * Both layouts are read; keep writing this one until the oldest stable a player
 * can switch back to reads the 72-byte layout too.
 */
TbBool save_high_score_table(void)
{
    char* fname = prepare_file_path(FGrp_Save, campaign.hiscore_fname);
    long fsize = campaign.hiscore_count * HISCORE_ENTRY_SIZE_LP64;
    if (fsize <= 0)
        return true;
    if (campaign.hiscore_table == NULL)
        return false;
    unsigned char *buf = (unsigned char *)calloc(fsize, 1);
    if (buf == NULL)
        return false;
    for (unsigned long i = 0; i < campaign.hiscore_count; i++)
    {
        unsigned char *dst = buf + i * HISCORE_ENTRY_SIZE_LP64;
        const struct HighScore *hscore = &campaign.hiscore_table[i];
        int64_t score = hscore->score;
        memcpy(dst, &score, sizeof(score));
        memcpy(dst + 8, hscore->name, HISCORE_NAME_LENGTH);
        memcpy(dst + 8 + HISCORE_NAME_LENGTH, &hscore->lvnum, sizeof(hscore->lvnum));
    }
    TbBool saved = (LbFileSaveAt(fname, buf, fsize) == fsize);
    free(buf);
    return saved;
}



/**
 * Adds new entry to high score table. Returns its index.
 */
int add_high_score_entry(unsigned long score, LevelNumber lvnum, const char *name)
{
    int dest_idx;
    // If the table is not initiated - return
    if (campaign.hiscore_table == NULL)
    {
        WARNMSG("Can't add entry to uninitiated high score table");
        return false;
    }
    // Determining position of the new entry to keep table sorted with decreasing scores
    for (dest_idx=0; dest_idx < campaign.hiscore_count; dest_idx++)
    {
        if (campaign.hiscore_table[dest_idx].score < score)
            break;
        if (campaign.hiscore_table[dest_idx].lvnum <= 0)
            break;
    }
    // Find different entry which has duplicates with higher score - the one we can overwrite with no consequence
    // Don't allow replacing first 10 scores - they are visible to the player, and shouldn't be touched
    int overwrite_idx;
    for (overwrite_idx = campaign.hiscore_count-1; overwrite_idx >= 10; overwrite_idx--)
    {
        LevelNumber last_lvnum = campaign.hiscore_table[overwrite_idx].lvnum;
        if (last_lvnum <= 0) {
            // Found unused slot
            break;
        }
        int k;
        for (k=overwrite_idx-1; k >= 0; k--)
        {
            if (campaign.hiscore_table[k].lvnum == last_lvnum) {
                // Found a duplicate entry for that level with higher score
                break;
            }
        }
        if (k >= 0)
            break;
    }
    SYNCDBG(4,"New high score entry index %d, overwrite index %d",(int)dest_idx,(int)overwrite_idx);
    // In case nothing was found to overwrite
    if (overwrite_idx < 10) {
        overwrite_idx = campaign.hiscore_count;
    }
    // If index from sorting is outside array, make it equal to index to overwrite
    if (dest_idx >= campaign.hiscore_count)
        dest_idx = overwrite_idx;
    // And overwrite index - if not found, make it point to last entry in array
    if (overwrite_idx >= campaign.hiscore_count)
        overwrite_idx = campaign.hiscore_count-1;
    // If the new score is too poor, and there's not enough space for it, and we couldn't find slot to overwrite, return
    if (dest_idx >= campaign.hiscore_count) {
        WARNMSG("Can't add entry to high score table - it's full and has no duplicating levels");
        return -1;
    }
    // Now, we need to shift entries between destination position and position to overwrite
    if (overwrite_idx > dest_idx)
    {
        // Moving entries down
        for (int k = overwrite_idx - 1; k >= dest_idx; k--)
        {
            memcpy(&campaign.hiscore_table[k+1],&campaign.hiscore_table[k],sizeof(struct HighScore));
        }
    } else
    {
        // Moving entries up
        for (int k = overwrite_idx; k < dest_idx; k++)
        {
            memcpy(&campaign.hiscore_table[k],&campaign.hiscore_table[k+1],sizeof(struct HighScore));
        }
    }
    // Preparing the new entry
    snprintf(campaign.hiscore_table[dest_idx].name, HISCORE_NAME_LENGTH, "%s", name);
    campaign.hiscore_table[dest_idx].score = score;
    campaign.hiscore_table[dest_idx].lvnum = lvnum;
    return dest_idx;
}

/**
 * Returns highest score value for given level.
 */
unsigned long get_level_highest_score(LevelNumber lvnum)
{
    for (int idx = 0; idx < campaign.hiscore_count; idx++)
    {
        if ((campaign.hiscore_table[idx].lvnum == lvnum) && (strcmp(campaign.hiscore_table[idx].name, "Bullfrog") != 0))
        {
            return campaign.hiscore_table[idx].score;
        }
    }
    return 0;
}
