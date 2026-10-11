/******************************************************************************/
/** @file lua_readonly_io.h
 *     Read-only file access for level and campaign Lua scripts (this edition).
 * @par Comment:
 *     See lua_readonly_io.c.
 * @par Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef LUA_READONLY_IO_H
#define LUA_READONLY_IO_H

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;

/** Replace io.open with a read-only one, refuse os.remove, and stop require()
 *  loading native code. Call once on a fresh state, after the standard
 *  libraries are open and before any script runs. */
void lua_make_files_read_only(struct lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
