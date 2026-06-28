#include "pre_inc.h"
#include "bflib_render_glworld.h"
#include "post_inc.h"

TbBool gl_world_active = false;

TbBool glworld_get_window_rect(int *x, int *y, int *w, int *h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return false;
}

GLuint glworld_scene_texture(void)
{
    return 0;
}
