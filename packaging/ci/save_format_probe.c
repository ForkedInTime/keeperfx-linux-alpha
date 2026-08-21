/******************************************************************************/
/** @file save_format_probe.c
 *     Prints the save-file compatibility fingerprint of this source tree.
 * @par Purpose:
 *     A saved game is a raw memory dump of `struct Game`, and the only
 *     compatibility check the engine performs on load is whether the saved
 *     blob's length equals `sizeof(struct Game)` for the running build
 *     (see the `hdr.len != sizeof(struct Game)` test in src/game_saves.c).
 *     So `sizeof(struct Game)` IS the save format version, whether anyone
 *     meant it to be or not, and any field added anywhere inside it -- at any
 *     nesting depth, in any struct it contains -- silently invalidates every
 *     saved game in existence.
 *
 *     This program exists so that number can be measured and compared against
 *     a tracked baseline before a release goes out. It is built by linux.mk
 *     with $(KFX_CFLAGS) -- the exact flags, defines and include paths the
 *     engine's own C objects are compiled with -- so the size it reports is
 *     the size the shipped binary uses. Do not compute it any other way.
 *
 *     Driven by packaging/ci/check-save-format.sh.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include <stdio.h>

#include "game_legacy.h"
#include "thing_data.h"

int main(void)
{
    /* Machine-readable; the checker parses key=value and cares only about
       sizeof_struct_game. The rest is here to make a mismatch diagnosable:
       the usual cause is a field added to struct Thing, which THINGS_COUNT
       multiplies into a large jump. */
    printf("sizeof_struct_game=%zu\n", sizeof(struct Game));
    printf("sizeof_struct_thing=%zu\n", sizeof(struct Thing));
    printf("things_count=%d\n", THINGS_COUNT);
    return 0;
}
