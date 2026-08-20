/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render_gl.c
 *     GPU (OpenGL) present backend - "Seam A" of the graphics modernization,
 *     extended with "Seam A.2": a multi-pass post-processing pipeline.
 * @par Purpose:
 *     Replaces the CPU software present (SDL_BlitSurface + SDL_UpdateWindow
 *     Surface) with a GPU OpenGL present pipeline. The engine keeps rendering
 *     into an 8-bit (256-colour paletted) framebuffer; this module uploads that
 *     framebuffer into a GL_R8UI integer texture and a 256x1 RGBA8 palette
 *     texture, then a fragment shader does the palette lookup on the GPU.
 *
 *     Seam A.2 inserts a post-FX chain between the palette-LUT pass and the
 *     screen: the scene is rendered into an HDR (RGBA16F) offscreen FBO, a
 *     dual-filter bloom (downsample/upsample mip chain with a soft-knee
 *     bright-pass) makes lights glow, and a final composite pass tone-maps
 *     (ACES), colour-grades (warmth/contrast/saturation), vignettes and
 *     optionally sharpens the result before drawing to the screen with the
 *     existing aspect-preserving letterboxed viewport.
 *
 *     All post-FX parameters have strong "AAA torchlit dungeon" defaults and
 *     can be overridden at launch from KFX_* environment variables (no rebuild
 *     needed to tune the look). If any post-FX GL resource fails to initialise
 *     the backend logs it and gracefully falls back to the Seam A direct path
 *     (scene straight to screen); a total GL failure still falls back to the
 *     legacy CPU blit in bflib_video.c.
 * @par Comment:
 *     Self-contained: depends only on libepoxy (GL loader), SDL2 and the
 *     bflib logging helpers.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_render_gl.h"

#include "bflib_basics.h"
#include "bflib_render_glworld.h"
#include "bflib_video.h" // vsync_enabled

/* Set by RendererGL when a context is up; see the header for who reads it.
 * Sits above the _WIN32 split for visibility, not for portability: this file is
 * not in Makefile's object list, so it is a Linux-only translation unit and the
 * stubs at the bottom serve a Windows build that is not currently wired up. */
TbBool lbUseGLPresent = false;

#ifndef _WIN32
#include <epoxy/gl.h>
#include <stdlib.h>
#endif
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#ifndef _WIN32

/** Maximum number of bloom mip levels in the downsample/upsample chain. */
#define GL_BLOOM_MAX_MIPS 8

/** Tunable post-FX parameters. Defaults target a dark torchlit dungeon:
 *  the screen is mostly dark with bright torches/lava/heart, so bloom should
 *  make those pop without washing out the whole frame. Each field is
 *  overridden at init from its KFX_* environment variable if set. */
struct gl_postfx_params {
    int   enabled;          /**< KFX_POSTFX       master on/off (default 0 = faithful original; set 1 for atmosphere). */
    float bloom_intensity;  /**< KFX_BLOOM_INTENSITY  additive bloom mix (0.55). */
    float bloom_threshold;  /**< KFX_BLOOM_THRESHOLD  luma bright-pass cutoff (0.65). */
    float bloom_softknee;   /**< KFX_BLOOM_SOFTKNEE   soft-knee width 0..1 (0.5). */
    int   bloom_mips;       /**< KFX_BLOOM_RADIUS     mip count / glow radius (6). */
    float exposure;         /**< KFX_EXPOSURE     pre-tonemap multiply (1.0). */
    float contrast;         /**< KFX_CONTRAST     contrast about mid-grey (1.10). */
    float saturation;       /**< KFX_SATURATION   colour saturation (1.10). */
    float warmth;           /**< KFX_WARMTH       optional global orange nudge (0.0). */
    float vignette;         /**< KFX_VIGNETTE     edge darkening strength (0.25). */
    float sharpen;          /**< KFX_SHARPEN      unsharp amount (0.25). */
    /* Cinematic split-tone grade ("Close to the Sun" orange & teal). */
    float splittone;        /**< KFX_SPLITTONE       master split-tone strength (0.18). */
    float shadow_teal;      /**< KFX_SHADOW_TEAL     how teal the shadows go (0.5). */
    float highlight_warm;   /**< KFX_HIGHLIGHT_WARM  how orange the highlights go (0.5). */
    float blackpoint;       /**< KFX_BLACKPOINT      shadow crush / black-point lift (0.02). */
};

/** A single bloom mip: an FBO and its RGBA16F colour texture, at a given size. */
struct gl_bloom_mip {
    GLuint fbo;
    GLuint tex;
    int    w;
    int    h;
};

/** Backend state. All zeroed when the backend is not initialised. */
static struct {
    TbBool inited;
    SDL_Window *window;
    SDL_GLContext context;

    /* Seam A: palette-LUT pass. */
    GLuint program;      /**< palette-LUT program. */
    GLuint vao;
    GLuint vbo;
    GLuint tex_indexed;  /**< GL_R8UI texture holding the 8-bit indices. */
    GLuint tex_palette;  /**< 256x1 GL_RGBA8 palette LUT. */
    GLint  loc_indexed;
    GLint  loc_palette;
    int    fb_width;
    int    fb_height;

    /* Seam B composite: draw the GPU world under the 8-bit GUI/overlays.
     * Active only when gl_world_active. */
    GLuint prog_world;       /**< Textured fullscreen-quad blit of the world. */
    GLint  wd_loc_world;
    GLuint prog_gui;         /**< Palette-LUT with sentinel-discard over world. */
    GLint  gui_loc_indexed;
    GLint  gui_loc_palette;
    GLint  gui_loc_rect;     /**< Engine-window rect (x0,y0,x1,y1) in target pixels. */
    GLint  gui_loc_sentinel; /**< Sentinel palette index to discard in-rect. */

    /* Seam A.2: post-FX chain. Active only when postfx_active is true. */
    TbBool postfx_active;        /**< true once the whole chain is set up OK. */
    struct gl_postfx_params p;

    GLuint scene_fbo;            /**< RGBA16F scene target (fb_width x fb_height). */
    GLuint scene_tex;

    /* Bright-pass + bloom mip chain (half-res down to 1/64 etc). */
    GLuint prog_bright;
    GLint  br_loc_tex;
    GLint  br_loc_texel;
    GLint  br_loc_threshold;
    GLint  br_loc_knee;

    GLuint prog_down;
    GLint  dn_loc_tex;
    GLint  dn_loc_texel;

    GLuint prog_up;
    GLint  up_loc_tex;
    GLint  up_loc_texel;

    int    n_mips;
    struct gl_bloom_mip mips[GL_BLOOM_MAX_MIPS];

    /* Composite (to screen) pass. */
    GLuint prog_comp;
    GLint  cp_loc_scene;
    GLint  cp_loc_bloom;
    GLint  cp_loc_texel;
    GLint  cp_loc_bloom_intensity;
    GLint  cp_loc_exposure;
    GLint  cp_loc_contrast;
    GLint  cp_loc_saturation;
    GLint  cp_loc_warmth;
    GLint  cp_loc_vignette;
    GLint  cp_loc_sharpen;
    GLint  cp_loc_splittone;
    GLint  cp_loc_shadow_teal;
    GLint  cp_loc_highlight_warm;
    GLint  cp_loc_blackpoint;

    /* Plan B: truecolor movie present. */
    GLuint prog_movie;   /**< Plan B truecolor passthrough program. */
    GLuint tex_movie;    /**< RGBA movie-frame texture. */
    GLint  loc_movie_tex;
} gl;

/* ----------------------------------------------------------------------- */
/* Shader sources (GLSL 3.30 core).                                        */
/* ----------------------------------------------------------------------- */

/* Shared fullscreen-quad vertex shader. Emits a quad from gl_VertexID and a
 * V-flipped UV so the SDL top-left-origin framebuffer appears upright. */
static const char *vertex_src =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main(void)\n"
    "{\n"
    "    vec2 pos = vec2((gl_VertexID & 1), (gl_VertexID >> 1));\n"
    "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
    "    v_uv = vec2(pos.x, 1.0 - pos.y);\n"
    "}\n";

/* A non-flipping vertex shader for the intermediate FBO->FBO passes (bright,
 * down, up). Those operate purely in UV space so no flip is wanted; using a
 * matching-orientation pass keeps the bloom aligned to the scene. */
static const char *vertex_noflip_src =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main(void)\n"
    "{\n"
    "    vec2 pos = vec2((gl_VertexID & 1), (gl_VertexID >> 1));\n"
    "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
    "    v_uv = pos;\n"
    "}\n";

/* Seam A scene pass: palette lookup, now writing into the scene FBO. */
static const char *fragment_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform usampler2D u_indexed;\n"
    "uniform sampler2D u_palette;\n"
    "void main(void)\n"
    "{\n"
    "    uint idx = texture(u_indexed, v_uv).r;\n"
    "    outColor = texelFetch(u_palette, ivec2(int(idx), 0), 0);\n"
    "}\n";

/* Seam B world blit: copy the GPU world's RGBA16F scene texture straight into
 * the post-FX scene FBO. Both targets are full-resolution and stored in GL
 * bottom-up orientation, so a straight (non-flipping) UV copy aligns them. The
 * engine-window viewport that glworld rendered into maps 1:1 onto the same
 * region of the scene FBO. */
static const char *world_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D u_world;\n"
    "void main(void)\n"
    "{\n"
    "    outColor = vec4(texture(u_world, v_uv).rgb, 1.0);\n"
    "}\n";

/* Plan B: truecolor passthrough for movie frames (RGBA texture -> screen). */
static const char *movie_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D u_tex;\n"
    "void main(void){ fragColor = texture(u_tex, v_uv); }\n";

/* Seam B GUI composite: the palette-LUT pass, but with a transparency rule so
 * the GPU world (already drawn into the scene FBO) shows through where the
 * 8-bit framebuffer holds the reserved sentinel index inside the engine-window
 * rect. Outside the rect (the GUI side panel) and for any non-sentinel pixel
 * inside the rect (HUD overlays, tooltips, the hand, on-screen text, menus,
 * creatures/things) the palette colour is drawn on top.
 *
 * Uses the same V-flipping vertex shader as the plain scene pass so the GUI
 * aligns identically. The rect test is done in gl_FragCoord pixel space (scene
 * FBO, GL bottom-left origin) to stay orientation-independent and to match the
 * full-resolution rect glworld recorded. */
static const char *gui_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform usampler2D u_indexed;\n"
    "uniform sampler2D u_palette;\n"
    "uniform vec4 u_rect;\n"        /* x0,y0,x1,y1 in scene-FBO pixels. */
    "uniform uint u_sentinel;\n"
    "void main(void)\n"
    "{\n"
    "    uint idx = texture(u_indexed, v_uv).r;\n"
    "    vec2 fc = gl_FragCoord.xy;\n"
    "    bool in_rect = (fc.x >= u_rect.x) && (fc.x < u_rect.z)\n"
    "                && (fc.y >= u_rect.y) && (fc.y < u_rect.w);\n"
    "    if (in_rect && (idx == u_sentinel)) {\n"
    "        discard;\n"  /* let the GPU world below show through */
    "    }\n"
    "    outColor = texelFetch(u_palette, ivec2(int(idx), 0), 0);\n"
    "}\n";

/* Bright-pass: soft-knee luminance threshold into half-res RGBA16F. */
static const char *bright_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2  u_texel;\n"
    "uniform float u_threshold;\n"
    "uniform float u_knee;\n"
    "void main(void)\n"
    "{\n"
    "    vec3 c = texture(u_tex, v_uv).rgb;\n"
    "    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "    // Soft-knee curve (Unreal/Next-Gen style) around the threshold.\n"
    "    float knee = max(u_threshold * u_knee, 1e-4);\n"
    "    float soft = clamp(l - u_threshold + knee, 0.0, 2.0 * knee);\n"
    "    soft = (soft * soft) / (4.0 * knee + 1e-4);\n"
    "    float contrib = max(soft, l - u_threshold);\n"
    "    contrib = max(contrib, 0.0) / max(l, 1e-4);\n"
    "    outColor = vec4(c * contrib, 1.0);\n"
    "}\n";

/* Downsample: 13-tap "Next-Gen Post" / dual-filter box-ish kernel. */
static const char *down_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"   /* texel size of the SOURCE texture. */
    "void main(void)\n"
    "{\n"
    "    vec2 t = u_texel;\n"
    "    vec3 a = texture(u_tex, v_uv + t * vec2(-2.0, 2.0)).rgb;\n"
    "    vec3 b = texture(u_tex, v_uv + t * vec2( 0.0, 2.0)).rgb;\n"
    "    vec3 c = texture(u_tex, v_uv + t * vec2( 2.0, 2.0)).rgb;\n"
    "    vec3 d = texture(u_tex, v_uv + t * vec2(-2.0, 0.0)).rgb;\n"
    "    vec3 e = texture(u_tex, v_uv).rgb;\n"
    "    vec3 f = texture(u_tex, v_uv + t * vec2( 2.0, 0.0)).rgb;\n"
    "    vec3 g = texture(u_tex, v_uv + t * vec2(-2.0,-2.0)).rgb;\n"
    "    vec3 h = texture(u_tex, v_uv + t * vec2( 0.0,-2.0)).rgb;\n"
    "    vec3 i = texture(u_tex, v_uv + t * vec2( 2.0,-2.0)).rgb;\n"
    "    vec3 j = texture(u_tex, v_uv + t * vec2(-1.0, 1.0)).rgb;\n"
    "    vec3 k = texture(u_tex, v_uv + t * vec2( 1.0, 1.0)).rgb;\n"
    "    vec3 l = texture(u_tex, v_uv + t * vec2(-1.0,-1.0)).rgb;\n"
    "    vec3 m = texture(u_tex, v_uv + t * vec2( 1.0,-1.0)).rgb;\n"
    "    vec3 col = e * 0.125;\n"
    "    col += (a + c + g + i) * 0.03125;\n"
    "    col += (b + d + f + h) * 0.0625;\n"
    "    col += (j + k + l + m) * 0.125;\n"
    "    outColor = vec4(col, 1.0);\n"
    "}\n";

/* Upsample: 9-tap tent filter, additively blended onto the larger mip. */
static const char *up_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"   /* texel size of the SOURCE (smaller) texture. */
    "void main(void)\n"
    "{\n"
    "    vec2 t = u_texel;\n"
    "    vec3 col = texture(u_tex, v_uv).rgb * 4.0;\n"
    "    col += texture(u_tex, v_uv + vec2(-t.x, 0.0)).rgb * 2.0;\n"
    "    col += texture(u_tex, v_uv + vec2( t.x, 0.0)).rgb * 2.0;\n"
    "    col += texture(u_tex, v_uv + vec2( 0.0,-t.y)).rgb * 2.0;\n"
    "    col += texture(u_tex, v_uv + vec2( 0.0, t.y)).rgb * 2.0;\n"
    "    col += texture(u_tex, v_uv + vec2(-t.x,-t.y)).rgb;\n"
    "    col += texture(u_tex, v_uv + vec2( t.x,-t.y)).rgb;\n"
    "    col += texture(u_tex, v_uv + vec2(-t.x, t.y)).rgb;\n"
    "    col += texture(u_tex, v_uv + vec2( t.x, t.y)).rgb;\n"
    "    col *= (1.0 / 16.0);\n"
    "    outColor = vec4(col, 1.0);\n"
    "}\n";

/* Composite-to-screen: HDR combine, ACES tonemap, colour grade, vignette,
 * optional unsharp. Flip parity: the single V-flip for the whole chain happens
 * in the scene pass (which uses the flipping vertex_src to sample the index
 * texture), so the scene FBO is stored in standard GL bottom-up orientation.
 * This composite pass therefore uses the plain (non-flipping) vertex_noflip_src
 * and a straight UV mapping; the net result is upright on screen, matching
 * Seam A. The bloom passes in between are radially symmetric and orientation
 * neutral. */
static const char *comp_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D u_scene;\n"
    "uniform sampler2D u_bloom;\n"
    "uniform vec2  u_texel;\n"   /* texel size of the scene texture. */
    "uniform float u_bloom_intensity;\n"
    "uniform float u_exposure;\n"
    "uniform float u_contrast;\n"
    "uniform float u_saturation;\n"
    "uniform float u_warmth;\n"
    "uniform float u_vignette;\n"
    "uniform float u_sharpen;\n"
    "uniform float u_splittone;\n"
    "uniform float u_shadow_teal;\n"
    "uniform float u_highlight_warm;\n"
    "uniform float u_blackpoint;\n"
    "vec3 aces(vec3 x)\n"
    "{\n"
    "    // Narkowicz ACES filmic approximation.\n"
    "    const float a = 2.51; const float b = 0.03;\n"
    "    const float c = 2.43; const float d = 0.59; const float e = 0.14;\n"
    "    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);\n"
    "}\n"
    "vec3 sample_hdr(vec2 uv)\n"
    "{\n"
    "    vec3 s = texture(u_scene, uv).rgb;\n"
    "    vec3 b = texture(u_bloom, uv).rgb;\n"
    "    return s + b * u_bloom_intensity;\n"
    "}\n"
    "void main(void)\n"
    "{\n"
    "    vec2 uv = v_uv;\n"
    "    vec3 hdr = sample_hdr(uv);\n"
    "    // Optional unsharp on the HDR composite (crisps upscaled pixel art).\n"
    "    if (u_sharpen > 0.0) {\n"
    "        vec3 n = sample_hdr(uv + vec2(-u_texel.x, 0.0))\n"
    "               + sample_hdr(uv + vec2( u_texel.x, 0.0))\n"
    "               + sample_hdr(uv + vec2( 0.0,-u_texel.y))\n"
    "               + sample_hdr(uv + vec2( 0.0, u_texel.y));\n"
    "        hdr += (hdr * 4.0 - n) * (u_sharpen * 0.25);\n"
    "        hdr = max(hdr, 0.0);\n"
    "    }\n"
    "    // Exposure.\n"
    "    hdr *= u_exposure;\n"
    "    // ACES filmic tone map (cinematic highlight rolloff).\n"
    "    vec3 col = aces(hdr);\n"
    "    // Black-point crush: deepen shadows so they go rich and dark (the\n"
    "    // 'Close to the Sun' look has deep blacks, not muddy brown). Rescales\n"
    "    // so [blackpoint,1] maps back to [0,1].\n"
    "    col = clamp((col - u_blackpoint) / max(1.0 - u_blackpoint, 1e-4), 0.0, 1.0);\n"
    "    // Cinematic split-tone (orange & teal): tint shadows toward cool teal\n"
    "    // and highlights toward warm orange, leaving midtones near-neutral.\n"
    "    // Reference target colours (normalised tint directions).\n"
    "    {\n"
    "        float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "        // Smooth shadow / highlight weights; midtones get little of either.\n"
    "        float wShadow    = (1.0 - smoothstep(0.0, 0.5, luma));\n"
    "        float wHighlight = smoothstep(0.5, 1.0, luma);\n"
    "        vec3 tealDir   = vec3(-0.30, 0.10, 0.40);  // toward cool teal/blue.\n"
    "        vec3 orangeDir = vec3( 0.45, 0.16, -0.40); // toward warm orange.\n"
    "        col += u_splittone * u_shadow_teal    * wShadow    * tealDir;\n"
    "        col += u_splittone * u_highlight_warm * wHighlight * orangeDir;\n"
    "        col = clamp(col, 0.0, 1.0);\n"
    "    }\n"
    "    // Optional global warmth nudge (back-compat; default 0).\n"
    "    col.r += u_warmth;\n"
    "    col.b -= u_warmth;\n"
    "    col = clamp(col, 0.0, 1.0);\n"
    "    // Saturation around luma.\n"
    "    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "    col = mix(vec3(luma), col, u_saturation);\n"
    "    // Contrast around 0.5 mid-grey.\n"
    "    col = (col - 0.5) * u_contrast + 0.5;\n"
    "    col = clamp(col, 0.0, 1.0);\n"
    "    // Vignette: subtle darkening toward the edges.\n"
    "    vec2 vc = v_uv - 0.5;\n"
    "    float vig = 1.0 - u_vignette * dot(vc, vc) * 2.0;\n"
    "    col *= clamp(vig, 0.0, 1.0);\n"
    "    outColor = vec4(col, 1.0);\n"
    "}\n";

/* ----------------------------------------------------------------------- */

/** Drain and log any pending GL errors. Returns true if an error was seen. */
static TbBool gl_check_error(const char *where)
{
    TbBool had_error = false;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        LbErrorLog("gl_present: GL error 0x%04x at %s\n", (unsigned int)err, where);
        had_error = true;
    }
    return had_error;
}

/** Compile a single shader stage, logging the info log on failure. */
static GLuint gl_compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        LbErrorLog("gl_present: glCreateShader failed\n");
        return 0;
    }
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        LbErrorLog("gl_present: %s shader compile failed: %s\n",
            (type == GL_VERTEX_SHADER) ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/** Build a program from a vertex + fragment source pair. Returns 0 on failure. */
static GLuint gl_build_program_src(const char *vsrc, const char *fsrc)
{
    GLuint vs = gl_compile_shader(GL_VERTEX_SHADER, vsrc);
    if (vs == 0) {
        return 0;
    }
    GLuint fs = gl_compile_shader(GL_FRAGMENT_SHADER, fsrc);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    if (prog == 0) {
        LbErrorLog("gl_present: glCreateProgram failed\n");
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(prog, (GLsizei)sizeof(log), &len, log);
        LbErrorLog("gl_present: program link failed: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/** Build the Seam A palette-LUT shader program. Returns 0 on failure. */
static GLuint gl_build_program(void)
{
    return gl_build_program_src(vertex_src, fragment_src);
}

/** Build the Seam B world-composite programs (world blit + sentinel-discard
 *  GUI). Failure is non-fatal: the caller leaves them at 0 and the present
 *  path falls back to drawing the GUI without compositing the world. */
static void gl_build_world_programs(void)
{
    gl.prog_world = gl_build_program_src(vertex_noflip_src, world_src);
    if (gl.prog_world != 0) {
        gl.wd_loc_world = glGetUniformLocation(gl.prog_world, "u_world");
    }
    gl.prog_gui = gl_build_program_src(vertex_src, gui_src);
    if (gl.prog_gui != 0) {
        gl.gui_loc_indexed  = glGetUniformLocation(gl.prog_gui, "u_indexed");
        gl.gui_loc_palette  = glGetUniformLocation(gl.prog_gui, "u_palette");
        gl.gui_loc_rect     = glGetUniformLocation(gl.prog_gui, "u_rect");
        gl.gui_loc_sentinel = glGetUniformLocation(gl.prog_gui, "u_sentinel");
    }
}

/* ----------------------------------------------------------------------- */
/* Post-FX parameter loading from environment.                             */
/* ----------------------------------------------------------------------- */

static float gl_env_float(const char *name, float def)
{
    const char *v = getenv(name);
    if ((v == NULL) || (v[0] == '\0')) {
        return def;
    }
    char *end = NULL;
    double d = strtod(v, &end);
    if (end == v) {
        return def;
    }
    return (float)d;
}

static int gl_env_int(const char *name, int def)
{
    const char *v = getenv(name);
    if ((v == NULL) || (v[0] == '\0')) {
        return def;
    }
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) {
        return def;
    }
    return (int)n;
}

static void gl_load_postfx_params(struct gl_postfx_params *p)
{
    p->enabled         = gl_env_int  ("KFX_POSTFX",          0);
    p->bloom_intensity = gl_env_float("KFX_BLOOM_INTENSITY", 0.55f);
    p->bloom_threshold = gl_env_float("KFX_BLOOM_THRESHOLD", 0.65f);
    p->bloom_softknee  = gl_env_float("KFX_BLOOM_SOFTKNEE",  0.5f);
    p->bloom_mips      = gl_env_int  ("KFX_BLOOM_RADIUS",    6);
    p->exposure        = gl_env_float("KFX_EXPOSURE",        1.0f);
    p->contrast        = gl_env_float("KFX_CONTRAST",        1.10f);
    p->saturation      = gl_env_float("KFX_SATURATION",      1.10f);
    p->warmth          = gl_env_float("KFX_WARMTH",          0.0f);
    p->vignette        = gl_env_float("KFX_VIGNETTE",        0.25f);
    p->sharpen         = gl_env_float("KFX_SHARPEN",         0.25f);
    p->splittone       = gl_env_float("KFX_SPLITTONE",       0.18f);
    p->shadow_teal     = gl_env_float("KFX_SHADOW_TEAL",     0.5f);
    p->highlight_warm  = gl_env_float("KFX_HIGHLIGHT_WARM",  0.5f);
    p->blackpoint      = gl_env_float("KFX_BLACKPOINT",      0.02f);

    if (p->bloom_mips < 1) {
        p->bloom_mips = 1;
    }
    if (p->bloom_mips > GL_BLOOM_MAX_MIPS) {
        p->bloom_mips = GL_BLOOM_MAX_MIPS;
    }
    if (p->bloom_softknee < 0.0f) {
        p->bloom_softknee = 0.0f;
    }
    if (p->bloom_softknee > 1.0f) {
        p->bloom_softknee = 1.0f;
    }
}

/* ----------------------------------------------------------------------- */
/* Post-FX FBO/texture construction.                                       */
/* ----------------------------------------------------------------------- */

/** Allocate one RGBA16F colour texture + FBO at the given size, with LINEAR
 *  filtering and CLAMP_TO_EDGE. Returns true on a complete framebuffer. */
static TbBool gl_make_color_target(GLuint *out_fbo, GLuint *out_tex,
    int w, int h, GLint filter)
{
    GLuint tex = 0;
    GLuint fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0,
        GL_RGBA, GL_FLOAT, NULL);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LbErrorLog("gl_present: FBO incomplete (0x%04x) at %dx%d\n",
            (unsigned int)status, w, h);
        if (fbo != 0) {
            glDeleteFramebuffers(1, &fbo);
        }
        if (tex != 0) {
            glDeleteTextures(1, &tex);
        }
        return false;
    }
    *out_fbo = fbo;
    *out_tex = tex;
    return true;
}

/** Tear down the scene FBO and the bloom mip chain (but keep the programs). */
static void gl_destroy_postfx_targets(void)
{
    if (gl.scene_fbo != 0) {
        glDeleteFramebuffers(1, &gl.scene_fbo);
        gl.scene_fbo = 0;
    }
    if (gl.scene_tex != 0) {
        glDeleteTextures(1, &gl.scene_tex);
        gl.scene_tex = 0;
    }
    for (int i = 0; i < GL_BLOOM_MAX_MIPS; i++) {
        if (gl.mips[i].fbo != 0) {
            glDeleteFramebuffers(1, &gl.mips[i].fbo);
        }
        if (gl.mips[i].tex != 0) {
            glDeleteTextures(1, &gl.mips[i].tex);
        }
        gl.mips[i].fbo = 0;
        gl.mips[i].tex = 0;
        gl.mips[i].w = 0;
        gl.mips[i].h = 0;
    }
    gl.n_mips = 0;
}

/** Build (or rebuild) the scene FBO and bloom mip chain for fb_width x
 *  fb_height. Returns true on success; on failure all targets are freed. */
static TbBool gl_build_postfx_targets(int fb_width, int fb_height)
{
    gl_destroy_postfx_targets();

    /* Scene target: full resolution RGBA16F, LINEAR (bloom samples it). */
    if (!gl_make_color_target(&gl.scene_fbo, &gl.scene_tex,
            fb_width, fb_height, GL_LINEAR)) {
        return false;
    }

    /* Bloom mip chain: start at half resolution, halve each level. */
    int w = fb_width / 2;
    int h = fb_height / 2;
    if (w < 1) { w = 1; }
    if (h < 1) { h = 1; }

    int n = gl.p.bloom_mips;
    int made = 0;
    for (int i = 0; i < n; i++) {
        if ((w < 2) || (h < 2)) {
            break;  /* don't go below ~1px; stop the chain early. */
        }
        if (!gl_make_color_target(&gl.mips[made].fbo, &gl.mips[made].tex,
                w, h, GL_LINEAR)) {
            gl_destroy_postfx_targets();
            return false;
        }
        gl.mips[made].w = w;
        gl.mips[made].h = h;
        made++;
        w /= 2;
        h /= 2;
    }
    if (made < 1) {
        LbErrorLog("gl_present: bloom chain produced no usable mips\n");
        gl_destroy_postfx_targets();
        return false;
    }
    gl.n_mips = made;
    return true;
}

/** Build all post-FX programs and fetch uniform locations. Returns false on
 *  failure (caller will fall back to the direct present). */
static TbBool gl_build_postfx_programs(void)
{
    gl.prog_bright = gl_build_program_src(vertex_noflip_src, bright_src);
    if (gl.prog_bright == 0) {
        return false;
    }
    gl.br_loc_tex       = glGetUniformLocation(gl.prog_bright, "u_tex");
    gl.br_loc_texel     = glGetUniformLocation(gl.prog_bright, "u_texel");
    gl.br_loc_threshold = glGetUniformLocation(gl.prog_bright, "u_threshold");
    gl.br_loc_knee      = glGetUniformLocation(gl.prog_bright, "u_knee");

    gl.prog_down = gl_build_program_src(vertex_noflip_src, down_src);
    if (gl.prog_down == 0) {
        return false;
    }
    gl.dn_loc_tex   = glGetUniformLocation(gl.prog_down, "u_tex");
    gl.dn_loc_texel = glGetUniformLocation(gl.prog_down, "u_texel");

    gl.prog_up = gl_build_program_src(vertex_noflip_src, up_src);
    if (gl.prog_up == 0) {
        return false;
    }
    gl.up_loc_tex   = glGetUniformLocation(gl.prog_up, "u_tex");
    gl.up_loc_texel = glGetUniformLocation(gl.prog_up, "u_texel");

    gl.prog_comp = gl_build_program_src(vertex_noflip_src, comp_src);
    if (gl.prog_comp == 0) {
        return false;
    }
    gl.cp_loc_scene           = glGetUniformLocation(gl.prog_comp, "u_scene");
    gl.cp_loc_bloom           = glGetUniformLocation(gl.prog_comp, "u_bloom");
    gl.cp_loc_texel           = glGetUniformLocation(gl.prog_comp, "u_texel");
    gl.cp_loc_bloom_intensity = glGetUniformLocation(gl.prog_comp, "u_bloom_intensity");
    gl.cp_loc_exposure        = glGetUniformLocation(gl.prog_comp, "u_exposure");
    gl.cp_loc_contrast        = glGetUniformLocation(gl.prog_comp, "u_contrast");
    gl.cp_loc_saturation      = glGetUniformLocation(gl.prog_comp, "u_saturation");
    gl.cp_loc_warmth          = glGetUniformLocation(gl.prog_comp, "u_warmth");
    gl.cp_loc_vignette        = glGetUniformLocation(gl.prog_comp, "u_vignette");
    gl.cp_loc_sharpen         = glGetUniformLocation(gl.prog_comp, "u_sharpen");
    gl.cp_loc_splittone       = glGetUniformLocation(gl.prog_comp, "u_splittone");
    gl.cp_loc_shadow_teal     = glGetUniformLocation(gl.prog_comp, "u_shadow_teal");
    gl.cp_loc_highlight_warm  = glGetUniformLocation(gl.prog_comp, "u_highlight_warm");
    gl.cp_loc_blackpoint      = glGetUniformLocation(gl.prog_comp, "u_blackpoint");
    return true;
}

/** Destroy all post-FX programs. */
static void gl_destroy_postfx_programs(void)
{
    if (gl.prog_bright != 0) { glDeleteProgram(gl.prog_bright); gl.prog_bright = 0; }
    if (gl.prog_down   != 0) { glDeleteProgram(gl.prog_down);   gl.prog_down   = 0; }
    if (gl.prog_up     != 0) { glDeleteProgram(gl.prog_up);     gl.prog_up     = 0; }
    if (gl.prog_comp   != 0) { glDeleteProgram(gl.prog_comp);   gl.prog_comp   = 0; }
}

/** Initialise the whole post-FX chain (programs + targets). On any failure it
 *  cleans up and returns false; the caller then runs the Seam A direct path. */
static TbBool gl_postfx_init(int fb_width, int fb_height)
{
    if (!gl.p.enabled) {
        LbSyncLog("gl_present: post-FX disabled via KFX_POSTFX=0; using direct present\n");
        return false;
    }
    if (!gl_build_postfx_programs()) {
        LbErrorLog("gl_present: post-FX shader build failed; falling back to direct present\n");
        gl_destroy_postfx_programs();
        return false;
    }
    if (!gl_build_postfx_targets(fb_width, fb_height)) {
        LbErrorLog("gl_present: post-FX FBO build failed; falling back to direct present\n");
        gl_destroy_postfx_programs();
        return false;
    }
    if (gl_check_error("gl_postfx_init")) {
        LbErrorLog("gl_present: post-FX init reported GL errors; falling back to direct present\n");
        gl_destroy_postfx_targets();
        gl_destroy_postfx_programs();
        return false;
    }

    LbSyncLog("gl_present: post-FX active - bloom(intensity=%.3f threshold=%.3f "
        "softknee=%.3f mips=%d/%d) exposure=%.3f contrast=%.3f saturation=%.3f "
        "splittone(strength=%.3f shadow_teal=%.3f highlight_warm=%.3f blackpoint=%.3f) "
        "warmth=%.3f vignette=%.3f sharpen=%.3f scene=%dx%d RGBA16F (FBO status: complete)\n",
        gl.p.bloom_intensity, gl.p.bloom_threshold, gl.p.bloom_softknee,
        gl.n_mips, gl.p.bloom_mips, gl.p.exposure, gl.p.contrast, gl.p.saturation,
        gl.p.splittone, gl.p.shadow_teal, gl.p.highlight_warm, gl.p.blackpoint,
        gl.p.warmth, gl.p.vignette, gl.p.sharpen, fb_width, fb_height);
    return true;
}

TbBool gl_present_init(SDL_Window *window, int fb_width, int fb_height)
{
    if (gl.inited) {
        gl_present_shutdown();
    }
    memset(&gl, 0, sizeof(gl));
    if ((window == NULL) || (fb_width <= 0) || (fb_height <= 0)) {
        LbErrorLog("gl_present: invalid init arguments\n");
        return false;
    }
    gl.window = window;
    gl.fb_width = fb_width;
    gl.fb_height = fb_height;

    gl.context = SDL_GL_CreateContext(window);
    if (gl.context == NULL) {
        LbErrorLog("gl_present: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(window, gl.context)) {
        LbErrorLog("gl_present: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        gl_present_shutdown();
        return false;
    }
    /* Presentation pacing, overridable with KFX_VSYNC:
     *    1  vsync on  -- SwapWindow blocks until the next vblank.
     *   -1  adaptive  -- vsync when a frame is on time, tear instead of
     *                waiting a whole extra refresh when it is late.
     *    0  off       -- never block, tears freely.
     *
     * This matters because SwapWindow's wait lands inside the frame's measured
     * draw time. With plain vsync a frame that misses its vblank by a fraction
     * of a millisecond waits for the whole next one, which at 144Hz turns a
     * marginal frame into a visible ~7ms stall. Adaptive trades a tear for that
     * stall.
     *
     * The default (when KFX_VSYNC is unset) follows the game's own vsync_enabled
     * setting (src/bflib_video.h), so the in-game option controls this backend
     * the same as it controls the software one. vsync_enabled is a plain bool
     * and cannot express adaptive, which is why KFX_VSYNC survives as an
     * explicit power-user override: set it and it wins outright, letting
     * adaptive (or a forced on/off) be tested without touching the config.
     */
    {
        int want = gl_env_int("KFX_VSYNC", vsync_enabled ? 1 : 0);
        // SDL3 returns bool (true on success) where SDL2 returned 0 on success, so
        // these have to be negated rather than compared against 0 -- written the
        // SDL2 way they compile clean and log a failure every time they succeed.
        if (!SDL_GL_SetSwapInterval(want)) {
            LbWarnLog("gl_present: swap interval %d unavailable: %s\n", want, SDL_GetError());
            // Only the adaptive request (-1) falls back to plain vsync here. An
            // explicit off (0) that the driver/compositor refuses must NOT be
            // promoted to on -- that is the opposite of what was asked for, so
            // it is left alone and SwapWindow runs at whatever SDL left in effect.
            if ((want == -1) && !SDL_GL_SetSwapInterval(1)) {
                LbWarnLog("gl_present: adaptive vsync unavailable, vsync (SDL_GL_SetSwapInterval) fallback also unavailable: %s\n", SDL_GetError());
            }
        }
        // SDL3 reports the interval through an out-parameter instead of returning it.
        int interval_in_effect = 0;
        if (!SDL_GL_GetSwapInterval(&interval_in_effect)) {
            interval_in_effect = 0;
        }
        LbSyncLog("gl_present: swap interval requested %d, in effect %d\n",
            want, interval_in_effect);
    }

    /* Report the GL version / renderer string (confirms hardware GPU path). */
    {
        const GLubyte *ver = glGetString(GL_VERSION);
        const GLubyte *ren = glGetString(GL_RENDERER);
        const GLubyte *ven = glGetString(GL_VENDOR);
        LbSyncLog("gl_present: OpenGL %s present backend active (renderer: %s, vendor: %s)\n",
            (ver != NULL) ? (const char *)ver : "?",
            (ren != NULL) ? (const char *)ren : "?",
            (ven != NULL) ? (const char *)ven : "?");
    }

    gl.program = gl_build_program();
    if (gl.program == 0) {
        gl_present_shutdown();
        return false;
    }
    gl.loc_indexed = glGetUniformLocation(gl.program, "u_indexed");
    gl.loc_palette = glGetUniformLocation(gl.program, "u_palette");

    /* Index framebuffer texture: GL_R8UI integer, GL_NEAREST is mandatory. */
    glGenTextures(1, &gl.tex_indexed);
    glBindTexture(GL_TEXTURE_2D, gl.tex_indexed);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, fb_width, fb_height, 0,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);

    /* Palette texture: 256x1 RGBA8, GL_NEAREST (we texelFetch it). */
    glGenTextures(1, &gl.tex_palette);
    glBindTexture(GL_TEXTURE_2D, gl.tex_palette);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    /* Fullscreen-quad VAO. Core profile requires a bound VAO even though the
     * vertex shader synthesises positions from gl_VertexID (no VBO needed). */
    glGenVertexArrays(1, &gl.vao);
    glBindVertexArray(gl.vao);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);

    if (gl_check_error("gl_present_init")) {
        LbErrorLog("gl_present: init reported GL errors; falling back\n");
        gl_present_shutdown();
        return false;
    }

    /* Seam A.2: try to bring up the post-FX chain. If it fails for any reason
     * we keep the working Seam A direct-to-screen present (postfx_active=false). */
    gl_load_postfx_params(&gl.p);
    gl.postfx_active = gl_postfx_init(fb_width, fb_height);
    if (!gl.postfx_active) {
        /* Make sure no half-built GL state leaks an error into the next frame. */
        gl_check_error("gl_postfx_init_fallback");
    }

    /* Seam B: build the world-composite programs. Non-fatal on failure. */
    gl_build_world_programs();
    gl_check_error("gl_build_world_programs");

    gl.inited = true;
    return true;
}

void gl_present_set_palette(const SDL_Color *colors, int count)
{
    if (!gl.inited || (colors == NULL)) {
        return;
    }
    if (count > 256) {
        count = 256;
    }
    if (count <= 0) {
        return;
    }
    unsigned char rgba[256 * 4];
    for (int i = 0; i < count; i++) {
        rgba[i * 4 + 0] = colors[i].r;
        rgba[i * 4 + 1] = colors[i].g;
        rgba[i * 4 + 2] = colors[i].b;
        rgba[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, gl.tex_palette);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, count, 1,
        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    gl_check_error("gl_present_set_palette");
}

/* ----------------------------------------------------------------------- */
/* Frame present helpers.                                                  */
/* ----------------------------------------------------------------------- */

/** Upload the engine's 8-bit indices into the index texture, re-allocating it
 *  (and the post-FX targets) if the framebuffer size changed. */
static void gl_upload_indices(const void *fb_pixels, int fb_width, int fb_height, int pitch)
{
    if ((fb_width != gl.fb_width) || (fb_height != gl.fb_height)) {
        glBindTexture(GL_TEXTURE_2D, gl.tex_indexed);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, fb_width, fb_height, 0,
            GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
        gl.fb_width = fb_width;
        gl.fb_height = fb_height;

        /* Resize-aware realloc of the post-FX chain too. If it fails, drop to
         * the Seam A direct path rather than break the present. */
        if (gl.postfx_active) {
            if (!gl_build_postfx_targets(fb_width, fb_height)) {
                LbErrorLog("gl_present: post-FX resize failed; disabling post-FX\n");
                gl_destroy_postfx_targets();
                gl.postfx_active = false;
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, gl.tex_indexed);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_width, fb_height,
        GL_RED_INTEGER, GL_UNSIGNED_BYTE, fb_pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

/** Run the palette-LUT pass. If target_fbo==0 it draws to the screen with the
 *  given viewport; otherwise it fills the bound full-FBO viewport. */
static void gl_run_scene_pass(GLuint target_fbo, int vp_x, int vp_y, int vp_w, int vp_h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vp_x, vp_y, vp_w, vp_h);

    glUseProgram(gl.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.tex_indexed);
    glUniform1i(gl.loc_indexed, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gl.tex_palette);
    glUniform1i(gl.loc_palette, 1);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

/** Draw a fullscreen quad with the bound program into the bound FBO. */
static void gl_draw_quad(void)
{
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

/** Build the bloom: bright-pass the scene into mip 0, downsample the chain,
 *  then additively upsample back up. The final wide blur is in mip[0].tex. */
static void gl_run_bloom(void)
{
    glDisable(GL_BLEND);

    /* Bright-pass: scene -> mip 0. Source texel size is the scene's. */
    glBindFramebuffer(GL_FRAMEBUFFER, gl.mips[0].fbo);
    glViewport(0, 0, gl.mips[0].w, gl.mips[0].h);
    glUseProgram(gl.prog_bright);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.scene_tex);
    glUniform1i(gl.br_loc_tex, 0);
    glUniform2f(gl.br_loc_texel, 1.0f / (float)gl.fb_width, 1.0f / (float)gl.fb_height);
    glUniform1f(gl.br_loc_threshold, gl.p.bloom_threshold);
    glUniform1f(gl.br_loc_knee, gl.p.bloom_softknee);
    gl_draw_quad();

    /* Downsample chain: mip i -> mip i+1. */
    glUseProgram(gl.prog_down);
    glUniform1i(gl.dn_loc_tex, 0);
    for (int i = 1; i < gl.n_mips; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, gl.mips[i].fbo);
        glViewport(0, 0, gl.mips[i].w, gl.mips[i].h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl.mips[i - 1].tex);
        glUniform2f(gl.dn_loc_texel,
            1.0f / (float)gl.mips[i - 1].w, 1.0f / (float)gl.mips[i - 1].h);
        gl_draw_quad();
    }

    /* Upsample chain: mip i -> mip i-1, additively blended. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);
    glUseProgram(gl.prog_up);
    glUniform1i(gl.up_loc_tex, 0);
    for (int i = gl.n_mips - 1; i > 0; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, gl.mips[i - 1].fbo);
        glViewport(0, 0, gl.mips[i - 1].w, gl.mips[i - 1].h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl.mips[i].tex);
        glUniform2f(gl.up_loc_texel,
            1.0f / (float)gl.mips[i].w, 1.0f / (float)gl.mips[i].h);
        gl_draw_quad();
    }
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

/** Composite scene + bloom to the screen with tonemap / grade / vignette /
 *  sharpen, using the aspect-preserving letterboxed viewport. */
static void gl_run_composite(int vp_x, int vp_y, int vp_w, int vp_h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vp_x, vp_y, vp_w, vp_h);

    glUseProgram(gl.prog_comp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.scene_tex);
    glUniform1i(gl.cp_loc_scene, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gl.mips[0].tex);
    glUniform1i(gl.cp_loc_bloom, 1);

    glUniform2f(gl.cp_loc_texel, 1.0f / (float)gl.fb_width, 1.0f / (float)gl.fb_height);
    glUniform1f(gl.cp_loc_bloom_intensity, gl.p.bloom_intensity);
    glUniform1f(gl.cp_loc_exposure, gl.p.exposure);
    glUniform1f(gl.cp_loc_contrast, gl.p.contrast);
    glUniform1f(gl.cp_loc_saturation, gl.p.saturation);
    glUniform1f(gl.cp_loc_warmth, gl.p.warmth);
    glUniform1f(gl.cp_loc_vignette, gl.p.vignette);
    glUniform1f(gl.cp_loc_sharpen, gl.p.sharpen);
    glUniform1f(gl.cp_loc_splittone, gl.p.splittone);
    glUniform1f(gl.cp_loc_shadow_teal, gl.p.shadow_teal);
    glUniform1f(gl.cp_loc_highlight_warm, gl.p.highlight_warm);
    glUniform1f(gl.cp_loc_blackpoint, gl.p.blackpoint);

    gl_draw_quad();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

/** Seam B scene build: composite the GPU world UNDER the 8-bit GUI/overlays
 *  into the given target. (1) Blit the world scene texture into the
 *  engine-window rect; (2) run the palette-LUT over the whole framebuffer with
 *  a sentinel-discard rule so the world shows through inside the rect where the
 *  8-bit pixel is the reserved sentinel, while the GUI panel and any overlay
 *  drawn over the world (HUD, tooltips, hand, text, menus, creatures) draw on
 *  top.
 *
 *  When target_fbo==gl.scene_fbo (post-FX path) the viewport is the full FBO
 *  and the rect is in FBO pixels; the unchanged bloom + composite chain then
 *  runs over the result. When target_fbo==0 (direct path) it draws straight to
 *  the screen with the letterboxed viewport, and the rect is mapped into that
 *  viewport's pixel space so the gl_FragCoord sentinel test stays aligned.
 *
 *  Returns true if the world was composited; false if the world programs or
 *  rect are unavailable (caller then falls back to the plain scene pass). */
static TbBool gl_run_world_composite(GLuint target_fbo,
    int vp_x, int vp_y, int vp_w, int vp_h)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;
    if ((gl.prog_world == 0) || (gl.prog_gui == 0)) {
        return false;
    }
    if (!glworld_get_window_rect(&rx, &ry, &rw, &rh)) {
        return false;
    }

    /* Map the FBO-space engine-window rect into the target viewport's pixel
     * space (identity when drawing full-FBO into the scene target). */
    float sx = (float)vp_w / (float)gl.fb_width;
    float sy = (float)vp_h / (float)gl.fb_height;
    float gx0 = (float)vp_x + (float)rx * sx;
    float gy0 = (float)vp_y + (float)ry * sy;
    float gx1 = gx0 + (float)rw * sx;
    float gy1 = gy0 + (float)rh * sy;

    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vp_x, vp_y, vp_w, vp_h);
    glDisable(GL_BLEND);

    /* (1) Blit the world into the engine-window rect only. The scissor keeps
     * the GUI panel area untouched (black), so the world cannot bleed under
     * the side panel even though the quad is fullscreen. */
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)(gx0 + 0.5f), (int)(gy0 + 0.5f),
        (int)(gx1 - gx0 + 0.5f), (int)(gy1 - gy0 + 0.5f));
    glUseProgram(gl.prog_world);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glworld_scene_texture());
    glUniform1i(gl.wd_loc_world, 0);
    gl_draw_quad();
    glDisable(GL_SCISSOR_TEST);

    /* (2) Palette-LUT GUI on top, discarding the sentinel inside the rect. */
    glUseProgram(gl.prog_gui);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.tex_indexed);
    glUniform1i(gl.gui_loc_indexed, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gl.tex_palette);
    glUniform1i(gl.gui_loc_palette, 1);
    glUniform4f(gl.gui_loc_rect, gx0, gy0, gx1, gy1);
    glUniform1ui(gl.gui_loc_sentinel, (GLuint)GL_WORLD_SENTINEL_INDEX);
    gl_draw_quad();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    return true;
}

void gl_present_frame(const void *fb_pixels, int fb_width, int fb_height, int pitch)
{
    if (!gl.inited || (fb_pixels == NULL)) {
        return;
    }

    gl_upload_indices(fb_pixels, fb_width, fb_height, pitch);

    /* Aspect-preserving letterbox fit into the current drawable. */
    int draw_w = 0;
    int draw_h = 0;
    // SDL3 dropped SDL_GL_GetDrawableSize; SDL_GetWindowSizeInPixels is the
    // replacement and reports the same thing (pixels, not logical units, which
    // is what the viewport needs on a HiDPI display).
    SDL_GetWindowSizeInPixels(gl.window, &draw_w, &draw_h);
    if ((draw_w <= 0) || (draw_h <= 0)) {
        return;
    }
    int vp_w = draw_w;
    int vp_h = (int)((long long)draw_w * fb_height / fb_width);
    if (vp_h > draw_h) {
        vp_h = draw_h;
        vp_w = (int)((long long)draw_h * fb_width / fb_height);
    }
    int vp_x = (draw_w - vp_w) / 2;
    int vp_y = (draw_h - vp_h) / 2;

    /* Seam B: when the GPU world is active, the scene is built by compositing
     * the GPU world UNDER the 8-bit GUI/overlays (world programs permitting). */
    if (gl.postfx_active) {
        /* Post-FX path: composite into the scene FBO (full res), then run the
         * unchanged bloom + composite chain over it. */
        TbBool world_composited = false;
        if (gl_world_active) {
            world_composited = gl_run_world_composite(gl.scene_fbo,
                0, 0, gl.fb_width, gl.fb_height);
        }
        if (!world_composited) {
            gl_run_scene_pass(gl.scene_fbo, 0, 0, gl.fb_width, gl.fb_height);
        }
        gl_run_bloom();
        gl_run_composite(vp_x, vp_y, vp_w, vp_h);
    } else {
        /* Seam A direct path: scene straight to the default framebuffer. With
         * the world active, composite it under the GUI directly to the screen
         * using the letterboxed viewport; otherwise the plain palette-LUT. */
        TbBool world_composited = false;
        if (gl_world_active) {
            world_composited = gl_run_world_composite(0, vp_x, vp_y, vp_w, vp_h);
        }
        if (!world_composited) {
            gl_run_scene_pass(0, vp_x, vp_y, vp_w, vp_h);
        }
    }

    SDL_GL_SwapWindow(gl.window);
}

void gl_present_frame_rgba(const void *rgba, int w, int h, int pitch)
{
    if (!gl.inited || (rgba == NULL) || (w <= 0) || (h <= 0)) {
        return;
    }
    if (gl.prog_movie == 0) {
        gl.prog_movie = gl_build_program_src(vertex_src, movie_src);
        if (gl.prog_movie == 0) {
            return;
        }
        gl.loc_movie_tex = glGetUniformLocation(gl.prog_movie, "u_tex");
    }
    if (gl.tex_movie == 0) {
        glGenTextures(1, &gl.tex_movie);
        glBindTexture(GL_TEXTURE_2D, gl.tex_movie);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindTexture(GL_TEXTURE_2D, gl.tex_movie);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    int draw_w = 0, draw_h = 0;
    // SDL3 dropped SDL_GL_GetDrawableSize; SDL_GetWindowSizeInPixels is the
    // replacement and reports the same thing (pixels, not logical units, which
    // is what the viewport needs on a HiDPI display).
    SDL_GetWindowSizeInPixels(gl.window, &draw_w, &draw_h);
    if ((draw_w <= 0) || (draw_h <= 0)) {
        return;
    }
    int vp_w = draw_w;
    int vp_h = (int)((long long)draw_w * h / w);
    if (vp_h > draw_h) { vp_h = draw_h; vp_w = (int)((long long)draw_h * w / h); }
    int vp_x = (draw_w - vp_w) / 2;
    int vp_y = (draw_h - vp_h) / 2;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, draw_w, draw_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vp_x, vp_y, vp_w, vp_h);

    glUseProgram(gl.prog_movie);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.tex_movie);
    glUniform1i(gl.loc_movie_tex, 0);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    SDL_GL_SwapWindow(gl.window);
}

TbBool gl_present_postfx_active(void)
{
    return gl.inited && gl.postfx_active;
}

void gl_present_shutdown(void)
{
    if (gl.context != NULL) {
        /* Make sure the context is current so deletes target the right one. */
        SDL_GL_MakeCurrent(gl.window, gl.context);
    }
    gl_destroy_postfx_targets();
    gl_destroy_postfx_programs();
    if (gl.vao != 0) {
        glDeleteVertexArrays(1, &gl.vao);
    }
    if (gl.vbo != 0) {
        glDeleteBuffers(1, &gl.vbo);
    }
    if (gl.tex_indexed != 0) {
        glDeleteTextures(1, &gl.tex_indexed);
    }
    if (gl.tex_palette != 0) {
        glDeleteTextures(1, &gl.tex_palette);
    }
    if (gl.program != 0) {
        glDeleteProgram(gl.program);
    }
    if (gl.prog_world != 0) {
        glDeleteProgram(gl.prog_world);
    }
    if (gl.prog_gui != 0) {
        glDeleteProgram(gl.prog_gui);
    }
    if (gl.prog_movie != 0) { glDeleteProgram(gl.prog_movie); gl.prog_movie = 0; }
    if (gl.tex_movie != 0)  { glDeleteTextures(1, &gl.tex_movie); gl.tex_movie = 0; }
    if (gl.context != NULL) {
        SDL_GL_DestroyContext(gl.context);
    }
    memset(&gl, 0, sizeof(gl));
}

#else /* _WIN32: GL present backend not built; provide stubs. */

TbBool gl_present_init(SDL_Window *window, int fb_width, int fb_height)
{
    (void)window; (void)fb_width; (void)fb_height;
    return false;
}
void gl_present_set_palette(const SDL_Color *colors, int count)
{
    (void)colors; (void)count;
}
void gl_present_frame(const void *fb_pixels, int fb_width, int fb_height, int pitch)
{
    (void)fb_pixels; (void)fb_width; (void)fb_height; (void)pitch;
}
void gl_present_frame_rgba(const void *rgba, int w, int h, int pitch)
{
    (void)rgba; (void)w; (void)h; (void)pitch;
}
TbBool gl_present_postfx_active(void)
{
    return false;
}
void gl_present_shutdown(void)
{
}

#endif /* _WIN32 */

/******************************************************************************/
#ifdef __cplusplus
}
#endif
