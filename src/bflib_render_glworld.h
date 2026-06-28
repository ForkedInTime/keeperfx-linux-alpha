#ifndef BFLIB_RENDER_GLWORLD_H
#define BFLIB_RENDER_GLWORLD_H

#include "bflib_basics.h"
#include <epoxy/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimal present-only stub: the GPU world renderer is excluded from this
 * build. gl_world_active is permanently false, so the present layer's
 * world-compositing branches are dead and it simply blits the 8-bit
 * framebuffer. */
/** Palette index reserved as the "no world pixel here" sentinel. Matches the
 * value used by the full world renderer in the dev tree. */
#define GL_WORLD_SENTINEL_INDEX 0

extern TbBool gl_world_active;
TbBool glworld_get_window_rect(int *x, int *y, int *w, int *h);
GLuint glworld_scene_texture(void);

#ifdef __cplusplus
}
#endif

#endif /* BFLIB_RENDER_GLWORLD_H */
