/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render_glworld.h
 *     GPU world renderer — "Seam B" scaffolding: offscreen scene FBO,
 *     lifecycle helpers, and a debug readback dump.
 * @par Purpose:
 *     Provides the module skeleton for the GPU truecolor world renderer.
 *     Task 1 creates the FBO and lifecycle wiring only; no geometry is
 *     submitted yet, so the game continues to render via the existing
 *     software path.
 * @par Comment:
 *     All GL code is fenced #ifndef _WIN32; on Windows the header exposes
 *     only the TbBool gl_world_active flag via a stub.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#pragma once
#ifndef BFLIB_RENDER_GLWORLD_H
#define BFLIB_RENDER_GLWORLD_H

#ifndef _WIN32
#include <epoxy/gl.h>
#endif
#include <stdint.h>
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One vertex of a world terrain triangle submitted to the GPU batch.
 *  sx/sy are engine-window-local pixel coordinates (top-left origin, in the
 *  same scaled space the software rasterizer uses). u/v are texture coords
 *  in 0..1 over the 32x32 tile. shade is the software shade level (S>>16),
 *  passed through unchanged so the fragment shader can reproduce the
 *  fade-table lookup for bit-exact parity. */
struct GlWorldVert {
    float sx, sy;
    float u, v;
    float shade;
};

/** True when the GL world module is active (Linux only, KFX_GLWORLD != "0",
 *  and glworld_init succeeded). Set/cleared by bflib_video. */
extern TbBool gl_world_active;

/** True when the hi-res terrain override path is active (Linux only, KFX_HIRES=1,
 *  glworld inited, and at least one override tile loaded). Set by glworld_hires_load. */
extern TbBool gl_hires_active;

/** True when the hi-res sprite override store is active (Linux only, KFX_HIRES_SPRITES=1,
 *  glworld inited, and at least one sprite frame loaded). Set by glworld_hires_sprites_load. */
extern TbBool gl_hires_sprites_active;

/** Reserved 8-bit palette index used as the "show the GPU world here" sentinel
 *  when compositing. When gl_world_active, the engine clears the engine-window
 *  rect of the 8-bit framebuffer to this index before drawing the world bucket
 *  (terrain goes to the GPU, not this buffer); the present path then discards
 *  this index inside the engine-window rect so the GPU world shows through.
 *  Index 0 is DK's conventional transparent/black background and is not used as
 *  a solid fill by GUI/overlay pixels in the 3D view. */
#define GL_WORLD_SENTINEL_INDEX 0

#ifndef _WIN32

/** Initialise the GL world module. Creates an RGBA16F offscreen scene FBO
 *  sized world_w x world_h using the GL context that is already current
 *  (created by gl_present_init). Must be called after gl_present_init.
 *
 * @param world_w  Width of the world render target, in pixels.
 * @param world_h  Height of the world render target, in pixels.
 * @return true on success; false on any GL/FBO failure (caller leaves
 *         gl_world_active false and the software path continues unchanged).
 */
TbBool glworld_init(int world_w, int world_h);

/** Begin a world frame: bind the scene FBO and set up the viewport for the
 *  engine sub-window. Clears to transparent black. No geometry is submitted
 *  by this task; later tasks call this before drawing.
 *
 * @param win_x  Engine window left edge in screen pixels (top-left origin).
 * @param win_y  Engine window top edge in screen pixels (top-left origin).
 * @param win_w  Engine window width in pixels.
 * @param win_h  Engine window height in pixels.
 */
void glworld_begin_frame(int win_x, int win_y, int win_w, int win_h);

/** End a world frame: unbind the scene FBO (restores default framebuffer). */
void glworld_end_frame(void);

/** Return the scene texture handle (RGBA16F, world_w x world_h).
 *  Valid only while the module is initialised. */
GLuint glworld_scene_texture(void);

/** Fetch the engine-window rectangle the world was last rendered into, in
 *  full-resolution scene-FBO pixels with a GL bottom-left origin (the same
 *  space glworld_scene_texture() lives in). Returns false before the first
 *  glworld_begin_frame, or if the module is not initialised. Any out pointer
 *  may be NULL. Used by the present path to composite the world into exactly
 *  the engine sub-window. */
TbBool glworld_get_window_rect(int *x, int *y, int *w, int *h);

/** Shut down the GL world module: delete FBO and texture resources. */
void glworld_shutdown(void);

/** Read back the scene FBO and write it to disk as a PNG (or PPM fallback).
 *  Useful for verifying clear colour / future rendered frames without a
 *  screen-capture tool.
 *
 * @param path  Output file path. The extension determines the format:
 *              ".png" writes a RGBA PNG via spng; anything else writes PPM.
 */
void glworld_debug_dump(const char *path);

/** (Re)build the GPU terrain texture store from the current paletted tiles
 *  (block_ptrs) and the active 256-colour palette (lbPaletteColors), plus the
 *  fade-table and palette lookup textures. Call on init and whenever the
 *  palette or block_ptrs change (e.g. terrain animation). Cheap enough to call
 *  once per world frame. */
void glworld_texstore_sync(void);

/** Mark the GPU tile atlas stale so the next glworld_texstore_sync re-uploads
 *  it. Call when the palette or block_ptrs change (palette set / animation). */
void glworld_texstore_mark_dirty(void);

/** Append one textured terrain triangle to the world batch. Vertices are in
 *  submission order (painter's). texture_id is the block_ptrs[] tile index. */
void glworld_submit_tri(const struct GlWorldVert v[3], uint16_t texture_id);

/* ----------------------------------------------------------------------- */
/* Sprites (billboards) and lines — Task 4.                                  */
/* ----------------------------------------------------------------------- */

/** Transparency modes for a submitted sprite, matching the software flags.
 *  The software path uses ordered dither for the TRANSPAR modes; the GPU
 *  approximates them with constant alpha (noted in the task report). */
enum GlWorldSpriteBlend {
    GLW_BLEND_OPAQUE = 0,   /**< Fully opaque (most sprites). */
    GLW_BLEND_TRANSPAR8,    /**< Lb_SPRITE_TRANSPAR8: ~50% (alpha 0.5). */
    GLW_BLEND_TRANSPAR4,    /**< Lb_SPRITE_TRANSPAR4: ~25% (alpha 0.25). */
    GLW_BLEND_ALPHA,        /**< Per-sprite alpha blend (alpha sprite). */
};

/** Submit one KeeperSprite frame as a GPU billboard quad, in submission order
 *  (painter's — interleaved with terrain triangles so occlusion is preserved).
 *
 *  The frame's RLE pixels are decoded once into the sprite atlas (index +
 *  coverage), keyed by frame_key. The active colour remap (lbSpriteReMapPtr)
 *  is uploaded as a 256-entry LUT, cached by pointer, and applied in the
 *  shader before the palette lookup, exactly like the software blit.
 *
 * @param dx,dy     Destination top-left in engine-window-local pixels.
 * @param dw,dh     Destination size in engine-window-local pixels.
 * @param src_data  Source RLE byte stream for this frame.
 * @param src_len   Upper bound (in bytes) of the source stream; the decoder
 *                  never reads past src_data+src_len even if a malformed frame
 *                  lacks a row terminator.
 * @param src_w,src_h Source frame dimensions in pixels.
 * @param frame_key Stable cache key for this exact frame (the kspr_idx).
 * @param remap     Active 256-byte remap table, or NULL for identity.
 * @param flip_x    True to mirror horizontally (Lb_SPRITE_FLIP_HORIZ).
 * @param blend     Transparency mode.
 */
void glworld_submit_keepersprite(int dx, int dy, int dw, int dh,
    const void *src_data, size_t src_len, int src_w, int src_h,
    uint32_t frame_key, const unsigned char *remap,
    TbBool flip_x, enum GlWorldSpriteBlend blend);

/** Submit a colored line (selection box) in engine-window-local pixels, in
 *  submission order. color is an 8-bit palette index (resolved to RGBA via the
 *  active palette LUT). */
void glworld_submit_line(float x0, float y0, float x1, float y1,
    unsigned char color);

/** True only between glworld_begin_frame and glworld_end_frame, i.e. while a
 *  GPU world frame is actually being built. The sprite/line interceptors in
 *  engine_render.c gate on this so the frontview draw list (which never opens a
 *  GPU world frame) keeps using the software path. */
TbBool glworld_frame_active(void);

/* ----------------------------------------------------------------------- */
/* Hi-res terrain override store (Task 2).                                  */
/* ----------------------------------------------------------------------- */

/** Reserved lookup value meaning "no hi-res override for this tile". */
#define GLW_HIRES_NONE 0xFFFFu
/** Edge length (px) of one hi-res override tile (square). */
#define GLW_HIRES_DIM  256

/** Load hi-res override tiles from `dir` (files tmap_<var>_<blockid>.png, RGBA8).
 *  Builds a GL_TEXTURE_2D_ARRAY (GLW_HIRES_DIM^2 x N, RGBA8, GL_REPEAT, GL_LINEAR,
 *  mipmapped) and a block_id->layer lookup texture (R16UI, sized like the tile atlas;
 *  GLW_HIRES_NONE elsewhere). Sets gl_hires_active true iff >=1 tile loads. Safe to
 *  call when KFX_HIRES is off (clears the store, leaves gl_hires_active false). */
void glworld_hires_load(const char *dir);
/** Delete override GL objects and free CPU buffers. */
void glworld_hires_shutdown(void);
/** Override array texture (0 if none). */
GLuint glworld_hires_array(void);
/** Override block_id->layer lookup texture (0 if none). */
GLuint glworld_hires_lookup_tex(void);
/** Number of override tiles loaded. */
int glworld_hires_count(void);

/* ----------------------------------------------------------------------- */
/* Hi-res sprite override store (Sub-project 3, Task 2).                   */
/* ----------------------------------------------------------------------- */

/** Edge length (px) of one hi-res sprite override layer (square).
 *  Each frame PNG is nearest-neighbour scaled to this square so UVs map
 *  0..1 directly to the array layer with no per-layer uv-rect needed. */
#define GLW_HIRES_SPR_DIM 256

/** Load sprite overrides from `dir` (files sprite_<frame_key>.png, RGBA8).
 *  Builds a GL_TEXTURE_2D_ARRAY (GLW_HIRES_SPR_DIM^2 x N, RGBA8, GL_LINEAR,
 *  GL_CLAMP_TO_EDGE) and a frame_key->layer open-addressed hash. Sets
 *  gl_hires_sprites_active true iff >=1 frame loads. Safe to call when
 *  KFX_HIRES_SPRITES is off (clears the store, leaves flag false). */
void glworld_hires_sprites_load(const char *dir);
/** Delete sprite override GL objects and free CPU buffers. */
void glworld_hires_sprites_shutdown(void);
/** Override sprite array texture (0 if none). */
GLuint glworld_hires_sprites_array(void);
/** Look up the array layer for frame_key; returns >=0 or -1 if no override. */
int glworld_hires_sprites_layer(uint32_t frame_key);

#endif /* !_WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* BFLIB_RENDER_GLWORLD_H */
