/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render_gl.h
 *     Header file for bflib_render_gl.c.
 * @par Purpose:
 *     GPU (OpenGL) present backend - "Seam A" of the graphics modernization.
 *     Takes the engine's 8-bit indexed framebuffer and presents it to the
 *     screen through an OpenGL pipeline, doing the palette lookup on the GPU.
 *     The interface is intentionally backend-agnostic so a future Vulkan
 *     backend could implement the same four entry points.
 * @par Comment:
 *     Just a header file - function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_RENDER_GL_H
#define BFLIB_RENDER_GL_H

#include "bflib_basics.h"

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/** True while the OpenGL present backend holds a live context.
 *
 * Owned by RendererGL's lifetime. Code that goes around the renderer seam and
 * talks to this backend directly -- the movie player, which presents truecolor
 * frames the 8-bit framebuffer cannot carry -- reads this to know whether that
 * is possible at all. Always false on Windows, where the backend is stubbed. */
extern TbBool lbUseGLPresent;

/** Initialise the OpenGL present backend.
 *
 * Creates an SDL_GLContext on the given (SDL_WINDOW_OPENGL) window, enables
 * vsync, compiles the palette-LUT shader, and allocates the index-framebuffer
 * texture, palette texture and fullscreen-quad geometry.
 *
 * @param window  SDL window, already created with SDL_WINDOW_OPENGL.
 * @param fb_width  Width of the engine's 8-bit framebuffer, in pixels.
 * @param fb_height Height of the engine's 8-bit framebuffer, in pixels.
 * @return true on success; false on any failure (caller should fall back to
 *         the legacy CPU blit). On failure the backend leaves itself shut down.
 */
TbBool gl_present_init(SDL_Window *window, int fb_width, int fb_height);

/** Upload the colour palette into the GPU palette texture.
 *
 * @param colors Array of SDL_Color (RGB used; A forced to 255).
 * @param count  Number of entries to upload (clamped to 256).
 */
void gl_present_set_palette(const SDL_Color *colors, int count);

/** Present one 8-bit indexed frame.
 *
 * Uploads the indices into the integer texture, sets an aspect-preserving
 * letterboxed viewport over the current drawable, runs the palette-LUT shader
 * over a fullscreen quad and swaps the window.
 *
 * @param fb_pixels Pointer to the 8-bit index data (one byte per pixel).
 * @param fb_width  Width in pixels.
 * @param fb_height Height in pixels.
 * @param pitch     Row stride in bytes (== row length in pixels for R8).
 */
void gl_present_frame(const void *fb_pixels, int fb_width, int fb_height, int pitch);

/** Present one RGBA truecolor frame letterboxed (Plan B: movie truecolor path).
 *
 * Uploads rgba into a GL_RGBA8 texture and draws it letterboxed to the screen.
 * Lazy-compiles the passthrough shader program on first call.
 * No-op when the GL backend is not initialised.
 *
 * @param rgba   Pointer to RGBA pixel data (4 bytes per pixel).
 * @param w      Frame width in pixels.
 * @param h      Frame height in pixels.
 * @param pitch  Row stride in bytes (== w*4 for packed rows).
 */
void gl_present_frame_rgba(const void *rgba, int w, int h, int pitch);

/** Tear down all GL objects and the GL context. Safe to call when not inited. */
void gl_present_shutdown(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
