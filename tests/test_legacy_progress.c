/*
 * fx1contn.sav as 64-bit Linux builds wrote it (campaign_flags as 8-byte longs)
 * must import into the int32_t IntralevelData without moving any other byte.
 *
 * decode_lp64_intralevel_v0() and its size macros are the real ones from
 * src/legacy_progress_lp64.c/.h, so this checks the code that ships. The
 * input is laid out by hand from the old declaration -- every byte before the
 * flags, 40 eight-byte flags, every byte after -- because the old header is no
 * longer in the tree; tests/run.sh's comment says how it was checked against a
 * file written by the old struct itself.
 */
#include "pre_inc.h"
#include "game_merge.h"
#include "config_campaigns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#include "legacy_progress_lp64.h"

// The 32-bit-long layout upstream's own import reads (LEGACY_PROGRESS_FILE_SIZE there)
#define LEGACY_PROGRESS_FILE_SIZE (CAMPAIGN_FNAME_LEN + sizeof(LevelNumber) + LEGACY_LP64_V0_SIZE)

static int failures = 0;

static void check(int cond, const char *what)
{
    printf("  %s   %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond)
        failures++;
}

static unsigned char pattern(size_t i)
{
    return (unsigned char)((i * 7 + 3) & 0xff);
}

int main(void)
{
    const size_t flags_at = offsetof(struct IntralevelData, campaign_flags);
    const size_t rest_at = offsetof(struct IntralevelData, next_level);
    const size_t old_rest_at = flags_at + LEGACY_LP64_FLAGS_COUNT * sizeof(int64_t);

    printf("legacy fx1contn.sav import (64-bit layout)\n");
    check(LEGACY_PROGRESS_FILE_SIZE == 67241, "the 32-bit-long file upstream imports is 67241 bytes");
    check(LEGACY_LP64_PROGRESS_FILE_SIZE == 67401, "the file 64-bit builds wrote is 67401 bytes");
    check(LEGACY_LP64_INTRALEVEL_SIZE - LEGACY_LP64_V0_SIZE == 160, "the only difference is 40 flags x 4 bytes");

    unsigned char *raw = malloc(LEGACY_LP64_INTRALEVEL_SIZE);
    struct IntralevelData *ilvl = malloc(sizeof(struct IntralevelData));
    if ((raw == NULL) || (ilvl == NULL))
        return 2;
    for (size_t i = 0; i < flags_at; i++)
        raw[i] = pattern(i);
    int64_t flags[LEGACY_LP64_FLAGS_COUNT];
    for (int i = 0; i < LEGACY_LP64_FLAGS_COUNT; i++)
        flags[i] = (int64_t)(i * 1000 - 7000);
    flags[3] = (int64_t)INT32_MAX + 5;
    flags[4] = (int64_t)INT32_MIN - 5;
    flags[5] = INT32_MAX;
    flags[6] = INT32_MIN;
    memcpy(raw + flags_at, flags, sizeof(flags));
    for (size_t i = old_rest_at; i < LEGACY_LP64_INTRALEVEL_SIZE; i++)
        raw[i] = pattern(i + 1000);

    memset(ilvl, 0, sizeof(struct IntralevelData));
    decode_lp64_intralevel_v0(raw, ilvl);
    const unsigned char *out = (const unsigned char *)ilvl;

    check(memcmp(out, raw, flags_at) == 0, "bonuses and transferred creatures are copied unchanged");
    int flags_ok = 1;
    for (int i = 0; i < LEGACY_LP64_FLAGS_COUNT; i++)
    {
        int64_t want = flags[i];
        if (want > INT32_MAX) want = INT32_MAX;
        if (want < INT32_MIN) want = INT32_MIN;
        if (ilvl->campaign_flags[i / CAMPAIGN_FLAGS_PER_PLAYER][i % CAMPAIGN_FLAGS_PER_PLAYER] != (int32_t)want)
            flags_ok = 0;
    }
    check(flags_ok, "every campaign flag keeps its value, in its player/slot");
    check(ilvl->campaign_flags[0][3] == INT32_MAX && ilvl->campaign_flags[0][4] == INT32_MIN,
        "values beyond 32 bits are clamped, keeping their sign");
    check(memcmp(out + rest_at, raw + old_rest_at, LEGACY_LP64_V0_SIZE - rest_at) == 0,
        "next level and ensign overrides are copied unchanged");
    int tail_zero = 1;
    for (size_t i = LEGACY_LP64_V0_SIZE; i < sizeof(struct IntralevelData); i++)
        if (out[i] != 0)
            tail_zero = 0;
    check(tail_zero, "fields the old file never had are left for the caller to fill");

    free(raw);
    free(ilvl);
    if (failures)
    {
        printf("%d legacy progress test(s) FAILED\n", failures);
        return 1;
    }
    printf("All legacy progress tests passed.\n");
    return 0;
}
